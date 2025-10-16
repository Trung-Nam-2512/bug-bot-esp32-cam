#pragma once
#include <esp_camera.h>

// fallback nếu lib thiếu UXGA
#ifndef FRAMESIZE_UXGA
#define FRAMESIZE_UXGA 10
#endif

namespace cfg
{
#define DEVICE_ID "cam-02"
#define TOPIC_CMD "BINHDUONG/ESP32CAM/" DEVICE_ID "/cmd"
#define TOPIC_STATUS "BINHDUONG/ESP32CAM/" DEVICE_ID "/status"

    // Wi-Fi (đa SSID)
    static constexpr const char *WIFI_SSIDS[] = {"Nha Tro Hung", "MyBackupAP"};
    static constexpr const char *WIFI_PASSWORDS[] = {"hoidelamgi@@123", "backup_pass"};
    static constexpr int WIFI_COUNT = 2;

    // Server HTTP nhận ảnh
    static constexpr const char *SERVER_URL = "http://192.168.1.2:2512/api/iot/cam/upload";
    static constexpr const char *AUTH_BEARER = "Bearer haha!2512@2003";

    // MQTT broker
    static constexpr const char *MQTT_HOST = "phuongnamdts.com";
    static constexpr uint16_t MQTT_PORT = 4783;
    static constexpr const char *MQTT_USER = "baonammqtt";
    static constexpr const char *MQTT_PASS = "mqtt@d1git";

    // Chu kỳ “nội bộ” (không dùng chụp định kỳ nữa, nhưng giữ để tick hệ thống)
    static constexpr uint32_t LOOP_TICK_MS = 20;

    // Camera: chất lượng cao + flash
    static constexpr int FRAME_SIZE = FRAMESIZE_UXGA; // 1600x1200 (cần PSRAM ổn)
    static constexpr int JPEG_QUALITY = 5;            // 10=đẹp (số càng nhỏ càng đẹp)

    static constexpr int FLASH_GPIO = 4;
    static constexpr bool FLASH_ACTIVE_HIGH = true; // nếu đèn sáng thì set LOW, đổi false
    static constexpr uint16_t PREFLASH_MS = 150;    // thời gian bật đèn trước chụp
    static constexpr uint8_t WARMUP_FRAMES = 0;

    // HMAC (khớp với server nếu bật)
    static constexpr const char *HMAC_KEY = "hahahahuhuhhu2512";
}
