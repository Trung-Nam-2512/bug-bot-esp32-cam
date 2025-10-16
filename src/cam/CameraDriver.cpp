#include "CameraDriver.h"
#include <Arduino.h>

// ==== AI Thinker ESP32-CAM pinout ====
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

using namespace cam;

bool CameraDriver::initPins() { return true; }

bool CameraDriver::begin(int frameSize, int jpegQuality, int flashGpio, bool flashActiveHigh)
{
    flashGpio_ = flashGpio;
    flashHigh_ = flashActiveHigh;

    pinMode(flashGpio_, OUTPUT);
    flashOff(); // đảm bảo tắt từ đầu

    camera_config_t config = {};
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;

    if (psramFound())
    {
        config.frame_size = (framesize_t)frameSize;
        config.jpeg_quality = jpegQuality;
        config.fb_count = 2;
    }
    else
    {
        config.frame_size = FRAMESIZE_UXGA; // ép luôn
        config.jpeg_quality = 5;
        config.fb_count = 1;
    }

    if (esp_camera_init(&config) != ESP_OK)
    {
        Serial.println("[E] Camera init failed");
        return false;
    }

    // Tuning nhẹ
    if (auto s = esp_camera_sensor_get())
    {
        s->set_framesize(s, (framesize_t)frameSize);
        s->set_quality(s, jpegQuality);
        s->set_gain_ctrl(s, 1);
        s->set_exposure_ctrl(s, 1);
        s->set_aec2(s, 1);
        s->set_lenc(s, 1);
        s->set_bpc(s, 1);
        s->set_wpc(s, 1);
    }
    return true;
}

void CameraDriver::flashOn() { digitalWrite(flashGpio_, flashHigh_ ? HIGH : LOW); }
void CameraDriver::flashOff() { digitalWrite(flashGpio_, flashHigh_ ? LOW : HIGH); }

// Bật flash -> preflash -> bỏ warmup frames -> chụp -> tắt flash
camera_fb_t *CameraDriver::captureHQ(uint8_t warmupFrames, uint16_t preFlashMs)
{
    flashOn();
    delay(preFlashMs);

    for (uint8_t i = 0; i < warmupFrames; ++i)
    {
        camera_fb_t *tmp = esp_camera_fb_get();
        if (tmp)
            esp_camera_fb_return(tmp);
        delay(25);
    }

    camera_fb_t *fb = esp_camera_fb_get();
    flashOff();
    return fb;
}

void CameraDriver::release(camera_fb_t *fb)
{
    if (fb)
        esp_camera_fb_return(fb);
}
