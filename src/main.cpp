#include <Arduino.h>
#include <time.h>

#include "Log.h"
#include "Config.h"

#include "cam/CameraDriver.h"
#include "net/WiFiHelper.h"
#include "net/HttpUploader.h"
#include "net/MqttClient.h"
#include "storage/SpiffsStore.h"

using namespace cfg;

// Cho HttpUploader dùng HMAC key (định nghĩa ĐÚNG namespace net)
namespace net
{
  const char *__esp32cam_hmac_key = cfg::HMAC_KEY;
}

// ==== Singletons ====
cam::CameraDriver camera;
net::WiFiHelper wifi;
net::HttpUploader http;
net::MqttClient mqtt;
storage::SpiffsStore store;

// ==== Runtime states ====
volatile bool g_captureRequested = false;
volatile bool g_autoMode = false;     // bật/tắt auto
volatile uint32_t g_intervalSec = 30; // chu kỳ auto (giây)
uint32_t g_lastAutoShotMs = 0;

// ==== Helpers ====
static void setupTimeNTP()
{
  configTime(0, 0, "time.google.com", "pool.ntp.org", "time.nist.gov");
  for (int i = 0; i < 50; ++i)
  {
    time_t now = time(nullptr);
    if (now > 1600000000)
    {
      LOGI("NTP synced: %lu", (unsigned long)now);
      return;
    }
    delay(100);
  }
  LOGW("NTP not synced, fallback millis()");
}

static uint32_t nowEpoch()
{
  time_t t = time(nullptr);
  return (t > 1600000000) ? (uint32_t)t : (millis() / 1000);
}

static bool uploadFrame(const uint8_t *buf, size_t len)
{
  const char *extra = "{\"fw\":\"esp32cam-mqtt-1.2\"}";
  return http.postJpeg(SERVER_URL, AUTH_BEARER, DEVICE_ID, buf, len, nowEpoch(), extra);
}

// Parse rất nhẹ (không kéo thêm lib)
static bool jsonFindBool(const String &s, const char *key, bool &out)
{
  int i = s.indexOf(key);
  if (i < 0)
    return false;
  i = s.indexOf(':', i);
  if (i < 0)
    return false;
  String t = s.substring(i + 1);
  t.trim();
  if (t.startsWith("true"))
  {
    out = true;
    return true;
  }
  if (t.startsWith("false"))
  {
    out = false;
    return true;
  }
  return false;
}
static bool jsonFindInt(const String &s, const char *key, int &out)
{
  int i = s.indexOf(key);
  if (i < 0)
    return false;
  i = s.indexOf(':', i);
  if (i < 0)
    return false;
  int j = i + 1;
  while (j < (int)s.length() && isspace((unsigned char)s[j]))
    j++;
  bool neg = (j < (int)s.length() && s[j] == '-');
  if (neg)
    j++;
  long v = 0;
  bool any = false;
  while (j < (int)s.length() && isdigit((unsigned char)s[j]))
  {
    v = v * 10 + (s[j] - '0');
    j++;
    any = true;
  }
  if (!any)
    return false;
  out = neg ? -(int)v : (int)v;
  return true;
}
static bool jsonHasCaptureCmd(const String &s)
{
  String t = s;
  t.toLowerCase();
  if (t == "capture")
    return true;
  return (t.indexOf("\"cmd\"") >= 0 && t.indexOf("capture") >= 0);
}

// ==== MQTT callback ====
// Hỗ trợ 2 nhóm lệnh:
// 1) Chụp ngay: "capture" hoặc {"cmd":"capture"}
// 2) Auto-config 1 lệnh: {"auto":true/false, "intervalSec":<3..3600>}
static void onMqttMessage(const String &topic, const uint8_t *payload, unsigned int len)
{
  String msg;
  msg.reserve(len);
  for (unsigned int i = 0; i < len; ++i)
    msg += (char)payload[i];
  msg.trim();

  // 1) Chụp ngay
  if (jsonHasCaptureCmd(msg))
  {
    g_captureRequested = true;
    mqtt.publish(TOPIC_STATUS, "{\"ack\":\"capture\"}");
    return;
  }

  // 2) Auto + Interval (gộp)
  bool touched = false;
  bool autoTmp;
  int secTmp;
  if (jsonFindBool(msg, "auto", autoTmp))
  {
    g_autoMode = autoTmp;
    touched = true;
  }
  if (jsonFindInt(msg, "intervalSec", secTmp))
  {
    if (secTmp < 3)
      secTmp = 3;
    if (secTmp > 3600)
      secTmp = 3600; // muốn dài hơn thì tăng ngưỡng ở đây
    g_intervalSec = (uint32_t)secTmp;
    touched = true;
  }

  if (touched)
  {
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"ok\":true,\"auto\":%s,\"intervalSec\":%u}",
             g_autoMode ? "true" : "false", (unsigned)g_intervalSec);
    mqtt.publish(TOPIC_STATUS, buf);
  }
  else
  {
    mqtt.publish(TOPIC_STATUS, "{\"warn\":\"unknown_cmd\"}");
  }
}

void setup()
{
  Serial.begin(115200);
  delay(200);
  LOGI("Booting (MQTT + Auto-config)…");

  if (!store.begin())
    LOGW("SPIFFS mount fail");

  // Wi-Fi (đa SSID)
  wifi.begin(WIFI_SSIDS, WIFI_PASSWORDS, WIFI_COUNT);

  setupTimeNTP();

  // Camera: preflash cố định, KHÔNG warmup (WARMUP_FRAMES = 0)
  if (!camera.begin(FRAME_SIZE, JPEG_QUALITY, FLASH_GPIO, FLASH_ACTIVE_HIGH))
  {
    LOGE("Camera init failed. Rebooting in 5s");
    delay(5000);
    ESP.restart();
  }

  // MQTT
  String clientId = String("esp32cam-") + DEVICE_ID;
  mqtt.begin(MQTT_HOST, MQTT_PORT, MQTT_USER, MQTT_PASS, clientId.c_str(), TOPIC_CMD, onMqttMessage);

  // Báo online
  mqtt.ensure();
  mqtt.publish(TOPIC_STATUS, "{\"online\":true}");
}

void loop()
{
  // Đảm bảo kết nối
  wifi.ensure();
  mqtt.ensure();
  mqtt.loop();

  // Auto scheduler
  if (g_autoMode)
  {
    uint32_t now = millis();
    if (now - g_lastAutoShotMs >= g_intervalSec * 1000UL)
    {
      g_captureRequested = true;
      g_lastAutoShotMs = now;
    }
  }

  // Thực thi chụp
  if (g_captureRequested)
  {
    g_captureRequested = false;

    camera_fb_t *fb = camera.captureHQ(cfg::WARMUP_FRAMES /*0*/, cfg::PREFLASH_MS /*150*/);
    if (!fb)
    {
      LOGW("Capture failed");
      mqtt.publish(TOPIC_STATUS, "{\"capture\":\"failed\"}");
      delay(200);
      return;
    }

    bool ok = (WiFi.status() == WL_CONNECTED) && uploadFrame(fb->buf, fb->len);
    if (!ok)
    {
      LOGW("Upload fail, queue");
      store.saveJpeg(fb->buf, fb->len); // lưu để flush sau
      mqtt.publish(TOPIC_STATUS, "{\"upload\":\"queued\"}");
    }
    else
    {
      mqtt.publish(TOPIC_STATUS, "{\"upload\":\"ok\"}");
    }
    camera.release(fb);

    // Thử flush 1 ảnh mới nhất trong queue (nếu có)
    if (WiFi.status() == WL_CONNECTED)
    {
      store.tryFlush([](const uint8_t *b, size_t l)
                     { return uploadFrame(b, l); });
    }
  }

  delay(LOOP_TICK_MS);
}
