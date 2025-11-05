#include <Arduino.h>
#include <time.h>
#include <ctype.h> // isspace, isdigit
#include <esp_task_wdt.h>

#include "Log.h"
#include "Config.h"

#include "cam/CameraDriver.h"
#include "net/WiFiHelper.h"
#include "net/HttpUploader.h"
#include "net/MqttClient.h"
#include "Log.h"
using namespace cfg;

// ===== Flash (GPIO4) via LEDC PWM =====
static const int FLASH_PIN = 4;     // AI-Thinker: GPIO4 điều khiển đèn flash
static const int FLASH_CH = 4;      // kênh PWM (chọn kênh trống)
static const int FLASH_FREQ = 5000; // 5 kHz
static const int FLASH_RES = 8;     // 8-bit (0..255)

// Tham số tinh chỉnh nhanh
static uint8_t FLASH_DUTY = 220;   // 0..255 (giảm nếu cháy sáng)
static uint16_t PREFLASH_MS = 120; // 60..150ms thường đẹp

static void flash_setup()
{
  ledcSetup(FLASH_CH, FLASH_FREQ, FLASH_RES);
  ledcAttachPin(FLASH_PIN, FLASH_CH);
  ledcWrite(FLASH_CH, 0); // tắt
}
static inline void flash_on(uint8_t duty) { ledcWrite(FLASH_CH, duty); }
static inline void flash_off() { ledcWrite(FLASH_CH, 0); }

// HMAC key cho HttpUploader (định nghĩa đúng namespace net)
namespace net
{
  const char *__esp32cam_hmac_key = cfg::HMAC_KEY;
}

// ===== Singletons =====
static cam::CameraDriver camera;
static net::WiFiHelper wifi;
static net::HttpUploader http;
static net::MqttClient mqtt;

// ===== Runtime states =====
static volatile bool g_captureRequested = false; // yêu cầu chụp (MQTT/auto)
static volatile bool g_autoMode = false;         // bật/tắt auto
static volatile uint32_t g_intervalSec = 30;     // chu kỳ auto (giây)
static volatile bool g_busy = false;             // chống chụp chồng lắp
static uint32_t g_lastAutoShotMs = 0;

static constexpr int kIntervalMin = 3;
static constexpr int kIntervalMax = 3600; // đổi nếu muốn dài hơn

// ===== Helpers =====
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

// Nếu  đã nâng HttpUploader để hỗ trợ shotId, hãy define macro này trong HttpUploader.h
// #define NET_HTTP_UPLOADER_SUPPORTS_SHOTID 1
static String makeShotId()
{
  char buf[48];
  snprintf(buf, sizeof(buf), "%s-%lu-%u",
           DEVICE_ID, (unsigned long)nowEpoch(), (unsigned)(millis() & 0xFFFF));
  return String(buf);
}

static bool uploadFrameLatest(const uint8_t *buf, size_t len)
{
  const char *extra = "{\"fw\":\"esp32cam-mqtt-1.2\"}";
#ifdef NET_HTTP_UPLOADER_SUPPORTS_SHOTID
  String shotId = makeShotId();
  LOGI("[DEBUG] SERVER_URL=%s", SERVER_URL);

  return http.postJpeg(SERVER_URL, AUTH_BEARER, DEVICE_ID,
                       buf, len, nowEpoch(), extra, shotId.c_str());
#else
  // Bản cũ: không có shotId
  return http.postJpeg(SERVER_URL, AUTH_BEARER, DEVICE_ID,
                       buf, len, nowEpoch(), extra);
#endif
}

// ===== JSON parse tối giản (không kéo thêm lib) =====
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
  while (j < (int)s.length())
  {
    char c = s[j];
    if (!isdigit((unsigned char)c))
      break;
    v = v * 10 + (c - '0');
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

static bool jsonHasResetCmd(const String &s)
{
  String t = s;
  t.toLowerCase();
  if (t == "reset")
    return true;
  return (t.indexOf("\"cmd\"") >= 0 && t.indexOf("reset") >= 0);
}

static bool jsonHasStatusCmd(const String &s)
{
  String t = s;
  t.toLowerCase();
  if (t == "status")
    return true;
  return (t.indexOf("\"cmd\"") >= 0 && t.indexOf("status") >= 0);
}

static bool jsonHasRestartCameraCmd(const String &s)
{
  String t = s;
  t.toLowerCase();
  if (t == "restart_camera")
    return true;
  return (t.indexOf("\"cmd\"") >= 0 && t.indexOf("restart_camera") >= 0);
}

// ===== MQTT callback =====
// 1) "capture" hoặc {"cmd":"capture"}
// 2) "reset" hoặc {"cmd":"reset"}
// 3) {"auto":true/false, "intervalSec":<3..3600>}
static void onMqttMessage(const String & /*topic*/, const uint8_t *payload, unsigned int len)
{
  String msg;
  msg.reserve(len);
  for (unsigned int i = 0; i < len; ++i)
    msg += (char)payload[i];
  msg.trim();

  // Reset ESP32
  if (jsonHasResetCmd(msg))
  {
    LOGI("[MQTT] Reset command detected!");
    mqtt.publish(TOPIC_STATUS, "{\"ack\":\"reset\",\"msg\":\"Restarting in 2 seconds...\"}");

    // Delay để đảm bảo message được gửi
    delay(2000);

    // Reset ESP32
    ESP.restart();
    return;
  }

  // Status info
  if (jsonHasStatusCmd(msg))
  {
    LOGI("[MQTT] Status command detected!");
    char statusMsg[512];
    snprintf(statusMsg, sizeof(statusMsg),
             "{\"ack\":\"status\",\"device\":\"%s\",\"wifi\":\"%s\",\"ip\":\"%s\",\"uptime\":%lu,\"free_heap\":%u,\"auto_mode\":%s,\"interval_sec\":%u,\"busy\":%s}",
             DEVICE_ID,
             WiFi.SSID().c_str(),
             WiFi.localIP().toString().c_str(),
             millis() / 1000,
             ESP.getFreeHeap(),
             g_autoMode ? "true" : "false",
             (unsigned)g_intervalSec,
             g_busy ? "true" : "false");
    mqtt.publish(TOPIC_STATUS, statusMsg);
    return;
  }

  // Restart camera
  if (jsonHasRestartCameraCmd(msg))
  {
    LOGI("[MQTT] Restart camera command detected!");
    mqtt.publish(TOPIC_STATUS, "{\"ack\":\"restart_camera\",\"msg\":\"Restarting camera...\"}");

    // Restart camera
    camera.end();
    delay(1000);
    if (camera.begin(FRAME_SIZE, JPEG_QUALITY))
    {
      mqtt.publish(TOPIC_STATUS, "{\"ack\":\"restart_camera\",\"result\":\"success\"}");
    }
    else
    {
      mqtt.publish(TOPIC_STATUS, "{\"ack\":\"restart_camera\",\"result\":\"failed\"}");
    }
    return;
  }

  // Chụp ngay
  if (jsonHasCaptureCmd(msg))
  {
    g_captureRequested = true;
    LOGI("[MQTT] Capture command detected, flag set!");

    mqtt.publish(TOPIC_STATUS, "{\"ack\":\"capture\"}");
    return;
  }

  // Auto + Interval (gộp 1 lệnh)
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
    if (secTmp < kIntervalMin)
      secTmp = kIntervalMin;
    if (secTmp > kIntervalMax)
      secTmp = kIntervalMax;
    g_intervalSec = (uint32_t)secTmp;
    touched = true;
  }

  if (touched)
  {
    char buf[96];
    snprintf(buf, sizeof(buf),
             "{\"ok\":true,\"auto\":%s,\"intervalSec\":%u}",
             g_autoMode ? "true" : "false",
             (unsigned)g_intervalSec);
    mqtt.publish(TOPIC_STATUS, buf);
  }
  else
  {
    mqtt.publish(TOPIC_STATUS, "{\"warn\":\"unknown_cmd\"}");
  }
}

// ===== Arduino lifecycle =====
void setup()
{
  Serial.begin(115200);
  delay(200);
  LOGI("Booting (MQTT + Auto-config, latest-only)…");

  // Khởi tạo watchdog timer (30 giây)
  esp_task_wdt_init(60, true);
  esp_task_wdt_add(NULL);

  // Wi-Fi đa SSID
  wifi.begin(WIFI_SSIDS, WIFI_PASSWORDS, WIFI_COUNT);
  setupTimeNTP();

  // Camera
  LOGI("Initializing camera...");

  if (!camera.begin(FRAME_SIZE, JPEG_QUALITY))
  {
    LOGE("Camera init failed. Reboot in 5s");
    delay(5000);
    ESP.restart();
  }
  flash_setup();
  LOGI("Camera initialized successfully");

  // MQTT
  String clientId = String("esp32cam-") + DEVICE_ID;
  mqtt.begin(MQTT_HOST, MQTT_PORT, MQTT_USER, MQTT_PASS,
             clientId.c_str(), TOPIC_CMD, onMqttMessage);

  mqtt.ensure();
  mqtt.publish(TOPIC_STATUS, "{\"online\":true}");
}

void loop()
{
  // Reset watchdog timer trong loop chính
  esp_task_wdt_reset();

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

  // Chụp & upload (chỉ frame mới nhất, không queue)
  // Chụp & upload (chỉ frame mới nhất, không queue)
  if (g_captureRequested && !g_busy)
  {
    g_captureRequested = false;
    g_busy = true;

    // Reset watchdog trước khi bắt đầu
    esp_task_wdt_reset();

    // ===== BẬT FLASH + tiền-flash cho AE/AWB ổn định =====
    LOGI("Flash ON (duty=%u), preflash %ums", FLASH_DUTY, (unsigned)PREFLASH_MS);
    flash_on(FLASH_DUTY);
    delay(PREFLASH_MS);

    // captureHQ sẽ warmup 'cfg::WARMUP_FRAMES' dưới ánh sáng flash
    camera_fb_t *fb = camera.captureHQ(cfg::WARMUP_FRAMES); // tham số trong Config.h :contentReference[oaicite:3]{index=3}

    // TẮT FLASH ngay sau khi có frame
    flash_off();

    if (!fb)
    {
      LOGW("Capture failed");
      mqtt.publish(TOPIC_STATUS, "{\"capture\":\"failed\"}");
      g_busy = false;
      esp_task_wdt_reset();
      delay(100);
      return;
    }

    LOGI("Captured image: %u bytes", fb->len);
    esp_task_wdt_reset();

    bool ok = (WiFi.status() == WL_CONNECTED) && uploadFrameLatest(fb->buf, fb->len);

    // Reset watchdog sau upload
    esp_task_wdt_reset();

    if (!ok)
    {
      LOGW("Upload fail");
      mqtt.publish(TOPIC_STATUS, "{\"upload\":\"fail\"}");
    }
    else
    {
      LOGI("Upload success");
      mqtt.publish(TOPIC_STATUS, "{\"upload\":\"ok\"}");
    }

    camera.release(fb);
    g_busy = false;

    // Reset watchdog cuối cùng
    esp_task_wdt_reset();
  }

  // Yield để cho phép các task khác chạy
  yield();
  delay(LOOP_TICK_MS);
}
