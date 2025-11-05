#include "HttpUploader.h"
#include "crypto/Crypto.h" // sha256Hex, hmacSha256Hex
#include <Arduino.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <esp_task_wdt.h>
#include <esp_system.h> // ESP.restart()
#include "Config.h"

namespace net
{

    extern const char *__esp32cam_hmac_key;

    void HttpUploader::splitUrl(const char *url, UrlParts &out)
    {
        String u(url ? url : "");
        out = UrlParts{};

        if (u.startsWith("https://"))
        {
            out.https = true;
            u.remove(0, 8); // bỏ "https://"
            out.port = 443;
        }
        else if (u.startsWith("http://"))
        {
            out.https = false;
            u.remove(0, 7); // bỏ "http://"
            out.port = 80;
        }
        else
        {
            out.https = false;
            out.port = 80;
        }

        int slash = u.indexOf('/');
        String hostPort = (slash >= 0) ? u.substring(0, slash) : u;
        out.path = (slash >= 0) ? u.substring(slash) : "/";

        int colon = hostPort.indexOf(':');
        if (colon >= 0)
        {
            out.host = hostPort.substring(0, colon);
            out.port = (uint16_t)hostPort.substring(colon + 1).toInt();
            if (out.port == 0)
                out.port = out.https ? 443 : 80;
        }
        else
        {
            out.host = hostPort;
        }

        // Debug log để xác nhận
        Serial.printf("[DEBUG] Parsed host=%s port=%u path=%s https=%d\n",
                      out.host.c_str(), out.port, out.path.c_str(), out.https);
    }

    int HttpUploader::readHttpStatus(Client &client, uint32_t waitMs)
    {
        uint32_t start = millis();
        while (millis() - start < waitMs)
        {
            while (client.available())
            {
                String line = client.readStringUntil('\n');
                line.trim();
                if (line.startsWith("HTTP/1.1 "))
                {
                    return line.substring(9).toInt(); // HTTP/1.1 <code>
                }
            }
            delay(5);
        }
        return -1; // timeout
    }

    bool HttpUploader::sendMultipart(Client &client,
                                     const UrlParts &u,
                                     const String &bearer,
                                     const String &signature,
                                     const String &shotId,
                                     const String &boundary,
                                     const String &bodyStart,
                                     const uint8_t *bin, size_t binLen,
                                     const String &bodyEnd)
    {
        // ---- HTTP/1.1 headers ----
        String req;
        req.reserve(512);
        req = "POST " + u.path + " HTTP/1.1\r\n";
        req += "Host: " + u.host + "\r\n";
        req += "User-Agent: ESP32-CAM/1.0\r\n";
        if (bearer.length())
            req += "Authorization: " + bearer + "\r\n";
        if (signature.length())
            req += "x-signature: " + signature + "\r\n";
        if (shotId.length())
            req += "x-shot-id: " + shotId + "\r\n";
        req += "Content-Type: multipart/form-data; boundary=" + boundary + "\r\n";
        req += "Connection: close\r\n";
        uint32_t contentLen = bodyStart.length() + binLen + bodyEnd.length();
        req += "Content-Length: " + String(contentLen) + "\r\n\r\n";

        Serial.printf("[DEBUG] Sending HTTP headers (%d bytes):\n%s\n", req.length(), req.c_str());
        if (client.print(req) != (int)req.length())
        {
            Serial.println("[E] Failed to send HTTP headers");
            return false;
        }
        esp_task_wdt_reset();
        yield();

        Serial.printf("[DEBUG] Sending body start (%d bytes)\n", bodyStart.length());
        if (client.print(bodyStart) != (int)bodyStart.length())
        {
            Serial.println("[E] Failed to send body start");
            return false;
        }
        esp_task_wdt_reset();
        yield();

        // ---- send binary in chunks (feed WDT per chunk) ----
        const size_t CHUNK = 2048; // 1–4KB đều ổn; 2048 an toàn
        size_t sent = 0;
        Serial.printf("[DEBUG] Sending binary data (%u bytes)\n", binLen);

        while (sent < binLen)
        {
            size_t n = binLen - sent;
            if (n > CHUNK)
                n = CHUNK;

            // handle partial write
            size_t writtenTotal = 0;
            while (writtenTotal < n)
            {
                size_t written = client.write(bin + sent + writtenTotal, n - writtenTotal);
                if (written == 0)
                {
                    // nếu mạng chậm, chờ chút rồi retry nhỏ
                    delay(2);
                    // nếu vẫn không tiến triển và socket đứt => fail
                    if (!client.connected())
                    {
                        Serial.printf("[E] Socket disconnected at %u/%u\n", sent + writtenTotal, binLen);
                        return false;
                    }
                    continue;
                }
                writtenTotal += written;

                // feed WDT/nhường CPU mỗi lần có tiến triển
                esp_task_wdt_reset();
                yield();
            }
            sent += writtenTotal;
        }
        Serial.printf("[DEBUG] Binary data sent successfully (%u bytes)\n", sent);
        esp_task_wdt_reset();
        yield();

        Serial.printf("[DEBUG] Sending body end (%d bytes)\n", bodyEnd.length());
        if (client.print(bodyEnd) != (int)bodyEnd.length())
        {
            Serial.println("[E] Failed to send body end");
            return false;
        }
        esp_task_wdt_reset();
        yield();

        // Đọc status với watchdog-safe loop
        const uint32_t RESP_WAIT_MS = 15000;
        uint32_t start = millis();
        int code = -1;
        while (millis() - start < RESP_WAIT_MS)
        {
            while (client.available())
            {
                String line = client.readStringUntil('\n');
                line.trim();
                if (line.startsWith("HTTP/1.1 "))
                {
                    code = line.substring(9).toInt();
                    break;
                }
            }
            if (code != -1)
                break;
            esp_task_wdt_reset();
            delay(5);
            yield();
        }
        Serial.printf("[DEBUG] HTTP response code: %d\n", code);
        // KHÔNG đọc body nữa để tránh block
        esp_task_wdt_reset(); // quan trọng
        yield();

        return (code == 200 || code == 201 || code == 204);

        // (Tùy chọn) đọc phần body để debug, watchdog-safe
        // String response;
        // start = millis();
        // while (millis() - start < 5000)
        // {
        //     while (client.available())
        //     {
        //         response += client.readString();
        //         start = millis(); // gia hạn nếu còn data
        //     }
        //     if (!client.available())
        //     {
        //         delay(5);
        //     }
        //     esp_task_wdt_reset();
        //     yield();
        // }
        // if (response.length())
        // {
        //     Serial.printf("[DEBUG] Server response body: %s\n", response.c_str());
        // }
        // else
        // {
        //     Serial.println("[DEBUG] No response body received");
        // }

        // bool success = (code == 200 || code == 201 || code == 204);
        // Serial.printf("[DEBUG] sendMultipart returning: %s (code=%d)\n", success ? "true" : "false", code);
        // return success;
    }

    bool HttpUploader::postJpeg(const char *url,
                                const char *bearer,
                                const char *deviceId,
                                const uint8_t *data,
                                size_t len,
                                uint32_t tsSec,
                                const char *extraJson,
                                const char *shotId)
    {
        if (WiFi.status() != WL_CONNECTED)
            return false;
        if (!url || !deviceId || !data || !len)
            return false;

        // ---- HMAC signature (optional) ----
        String signature;
        if (__esp32cam_hmac_key && __esp32cam_hmac_key[0])
        {
            String imgHash = sha256Hex(data, len);
            String message = String(deviceId) + "." + String(tsSec) + "." + imgHash;
            signature = hmacSha256Hex(__esp32cam_hmac_key, message);
        }

        UrlParts u;
        splitUrl(url, u);

        WiFiClient tcp;
        WiFiClientSecure tls;
        Client *stream = nullptr;

        if (u.https)
        {
            tls.setTimeout(60000);
            tls.setInsecure();    // Bỏ qua certificate verification
            tls.setNoDelay(true); // NEW: giảm delay Nagle

            Serial.printf("[DEBUG] Attempting HTTPS connection to %s:%u\n", u.host.c_str(), u.port);
            if (!tls.connect(u.host.c_str(), u.port))
            {
                Serial.printf("[E] HTTPS connect fail to %s:%u\n", u.host.c_str(), u.port);
                return false;
            }
            Serial.println("[DEBUG] HTTPS connection successful");
            stream = &tls;
        }
        else
        {
            tcp.setTimeout(60000); // NEW: tăng 60s
            tcp.setNoDelay(true);  // NEW: giảm delay Nagle            Serial.printf("[DEBUG] Attempting HTTP connection to %s:%u\n", u.host.c_str(), u.port);
            if (!tcp.connect(u.host.c_str(), u.port))
            {
                Serial.printf("[E] HTTP connect fail to %s:%u\n", u.host.c_str(), u.port);
                return false;
            }
            Serial.println("[DEBUG] HTTP connection successful");
            stream = &tcp;
        }

        const String boundary = "----esp32camboundary" + String(millis(), HEX);

        // ---- multipart body ----
        String bodyStart;
        bodyStart.reserve(512);
        bodyStart = "--" + boundary + "\r\n";
        bodyStart += "Content-Disposition: form-data; name=\"deviceId\"\r\n\r\n";
        bodyStart += String(deviceId) + "\r\n";

        bodyStart += "--" + boundary + "\r\n";
        bodyStart += "Content-Disposition: form-data; name=\"ts\"\r\n\r\n";
        bodyStart += String(tsSec) + "\r\n";

        if (extraJson && extraJson[0])
        {
            bodyStart += "--" + boundary + "\r\n";
            bodyStart += "Content-Disposition: form-data; name=\"extra\"\r\n\r\n";
            bodyStart += String(extraJson) + "\r\n";
        }

        bodyStart += "--" + boundary + "\r\n";
        bodyStart += "Content-Disposition: form-data; name=\"file\"; filename=\"shot.jpg\"\r\n";
        bodyStart += "Content-Type: image/jpeg\r\n\r\n";

        String bodyEnd = "\r\n--" + boundary + "--\r\n";

        String bearerStr = (bearer && bearer[0]) ? String(bearer) : String();
        String shotStr = (shotId && shotId[0]) ? String(shotId) : String();

        Serial.printf("[DEBUG] Sending multipart data, size=%u bytes\n", len);

        bool ok = sendMultipart(*stream, u, bearerStr, signature, shotStr,
                                boundary, bodyStart, data, len, bodyEnd);

        Serial.printf("[DEBUG] Upload result: %s\n", ok ? "SUCCESS" : "FAILED");
        Serial.printf("[DEBUG] ok = %s\n", ok ? "true" : "false");

        // RESTART ESP32-CAM SAU KHI UPLOAD THÀNH CÔNG
        if (ok)
        {
            Serial.println("[RESTART] Upload successful - restarting ESP32-CAM in 2 seconds...");
            delay(2000);
            ESP.restart();
        }
        // Đóng kết nối NGAY sau khi sendMultipart trả về
        esp_task_wdt_reset();
        // Đóng kết nối
        if (u.https)
            ((WiFiClientSecure *)stream)->stop();
        else
            ((WiFiClient *)stream)->stop();

        return ok;
    }

} // namespace net
