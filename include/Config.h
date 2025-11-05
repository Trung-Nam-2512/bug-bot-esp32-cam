#pragma once
#include <esp_camera.h>

// fallback nếu lib thiếu UXGA
#ifndef FRAMESIZE_UXGA
#define FRAMESIZE_UXGA 13 // 1600x1200
#endif

namespace cfg
{
#define DEVICE_ID "cam-03"
#define TOPIC_CMD "BINHDUONG/ESP32CAM/" DEVICE_ID "/cmd"
#define TOPIC_STATUS "BINHDUONG/ESP32CAM/" DEVICE_ID "/status"
#define NET_HTTP_UPLOADER_SUPPORTS_SHOTID 1

    // Wi-Fi (đa SSID)
    static constexpr const char *WIFI_SSIDS[] = {"Nha Tro Hung", "Nguyen"};
    static constexpr const char *WIFI_PASSWORDS[] = {"hoidelamgi@@123", "123456789@"};
    static constexpr int WIFI_COUNT = 2;

    // Server HTTP nhận ảnh
    // static constexpr const char *SERVER_URL = "http://ingest.bugbot.nguyentrungnam.com/api/iot/cam/upload";
    static constexpr const char *SERVER_URL = "http://192.168.1.12:1435/api/iot/cam/upload";

    static constexpr const char *AUTH_BEARER = "Bearer haha!2512@2003";

    // MQTT broker
    static constexpr const char *MQTT_HOST = "phuongnamdts.com";
    static constexpr uint16_t MQTT_PORT = 4783;
    static constexpr const char *MQTT_USER = "baonammqtt";
    static constexpr const char *MQTT_PASS = "mqtt@d1git";

    // Chu kỳ “nội bộ” (không dùng chụp định kỳ nữa, nhưng giữ để tick hệ thống)
    static constexpr uint32_t LOOP_TICK_MS = 20;

    // Camera: chất lượng cao
    static constexpr int FRAME_SIZE = FRAMESIZE_UXGA; // 1600x1200 (cần PSRAM ổn)
    static constexpr int JPEG_QUALITY = 5;            // 10=đẹp (số càng nhỏ càng đẹp)
    static constexpr uint8_t WARMUP_FRAMES = 2;       // warmup frames để ổn định

    // HMAC (khớp với server nếu bật)
    static constexpr const char *HMAC_KEY = "hahahahuhuhhu2512";
}
// Các cấu hình HTTPS đã được loại bỏ vì chuyển sang HTTP