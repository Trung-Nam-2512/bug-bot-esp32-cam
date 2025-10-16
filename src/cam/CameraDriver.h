#pragma once
#include <esp_camera.h>

namespace cam
{

    class CameraDriver
    {
    public:
        bool begin(int frameSize, int jpegQuality, int flashGpio, bool flashActiveHigh);
        // chụp có preflash + warmup frames
        camera_fb_t *captureHQ(uint8_t warmupFrames, uint16_t preFlashMs);
        void release(camera_fb_t *fb);

        // debug flash
        void flashOn();
        void flashOff();

    private:
        bool initPins();
        int flashGpio_ = 4;
        bool flashHigh_ = true; // true = HIGH bật đèn, false = LOW bật đèn
    };

} // namespace cam
