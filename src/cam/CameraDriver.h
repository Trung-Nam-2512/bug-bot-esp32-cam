#pragma once
#include <esp_camera.h>

namespace cam
{

    class CameraDriver
    {
    public:
        bool begin(int frameSize, int jpegQuality);
        void end();
        camera_fb_t *captureHQ(uint8_t warmupFrames);
        void release(camera_fb_t *fb);

    private:
        bool initPins();
    };

} // namespace cam
