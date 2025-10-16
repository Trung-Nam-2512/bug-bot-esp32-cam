#pragma once
#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <vector>
#include <functional>

namespace storage
{

    class SpiffsStore
    {
    public:
        bool begin();
        bool saveJpeg(const uint8_t *buf, size_t len);

        // Gửi ảnh mới nhất trước (LIFO). Uploader trả true nếu upload OK.
        void tryFlush(const std::function<bool(const uint8_t *, size_t)> &uploader);
    };

} // namespace storage
