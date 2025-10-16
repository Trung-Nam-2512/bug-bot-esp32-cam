#include "SpiffsStore.h"
#include <memory>
#include <algorithm>

using namespace storage;

bool SpiffsStore::begin()
{
    return SPIFFS.begin(true);
}

bool SpiffsStore::saveJpeg(const uint8_t *buf, size_t len)
{
    uint32_t ms = millis();
    String name = "/q_" + String(ms) + ".jpg"; // đảm bảo có "/" đầu
    File f = SPIFFS.open(name, FILE_WRITE);
    if (!f)
        return false;
    size_t w = f.write(buf, len);
    f.close();
    return w == len;
}

void SpiffsStore::tryFlush(const std::function<bool(const uint8_t *, size_t)> &uploader)
{
    // 1) Thu danh sách file
    std::vector<String> files;
    File root = SPIFFS.open("/");
    if (!root || !root.isDirectory())
        return;

    File file = root.openNextFile();
    while (file)
    {
        String name = file.name(); // dạng "/q_xxx.jpg"
        file.close();
        if (name.endsWith(".jpg"))
            files.push_back(name);
        file = root.openNextFile();
    }
    if (files.empty())
        return;

    // 2) Sắp xếp giảm dần => mới nhất trước
    std::sort(files.begin(), files.end(), [](const String &a, const String &b)
              { return a > b; });

    // 3) Gửi đúng 1 file mới nhất rồi thoát (giảm nghẽn)
    const String &name = files.front();
    File img = SPIFFS.open(name, FILE_READ);
    if (!img)
        return;

    size_t size = img.size();
    std::unique_ptr<uint8_t[]> buf(new uint8_t[size]);
    size_t r = img.read(buf.get(), size);
    img.close();
    if (r != size)
        return;

    bool ok = uploader(buf.get(), size);
    if (ok)
    {
        if (SPIFFS.remove(name))
        {
            Serial.printf("[I] Flushed & removed %s\n", name.c_str());
        }
        else
        {
            Serial.printf("[W] Remove failed for %s (path?)\n", name.c_str());
        }
        delay(200); // giãn nhịp
    }
    else
    {
        Serial.printf("[W] Flush fail, keep %s\n", name.c_str());
    }
}
