#include "HttpUploader.h"
#include "crypto/Crypto.h"
#include <Arduino.h>
#include <WiFiClient.h>

using namespace net;

static void splitUrl(const char *url, String &hostOut, uint16_t &portOut, String &pathOut)
{
    String u(url);
    hostOut = "";
    pathOut = "/";
    portOut = 80;
    if (u.startsWith("http://"))
        u.remove(0, 7);
    int slash = u.indexOf('/');
    String hostPort = (slash >= 0) ? u.substring(0, slash) : u;
    pathOut = (slash >= 0) ? u.substring(slash) : "/";
    int colon = hostPort.indexOf(':');
    if (colon >= 0)
    {
        hostOut = hostPort.substring(0, colon);
        portOut = (uint16_t)hostPort.substring(colon + 1).toInt();
    }
    else
        hostOut = hostPort;
}

bool HttpUploader::sendMultipart(WiFiClient &client,
                                 const String &host,
                                 const String &path,
                                 const String &bearer,
                                 const String &signature,
                                 const String &boundary,
                                 const String &bodyStart,
                                 const uint8_t *bin, size_t binLen,
                                 const String &bodyEnd)
{
    // HTTP/1.1 header
    String req;
    req.reserve(512);
    req += "POST " + path + " HTTP/1.1\r\n";
    req += "Host: " + host + "\r\n";
    req += "Authorization: " + bearer + "\r\n";
    if (signature.length())
        req += "x-signature: " + signature + "\r\n";
    req += "Content-Type: multipart/form-data; boundary=" + boundary + "\r\n";
    req += "Connection: close\r\n";
    uint32_t contentLen = bodyStart.length() + binLen + bodyEnd.length();
    req += "Content-Length: " + String(contentLen) + "\r\n\r\n";

    if (client.print(req) != (int)req.length())
        return false;
    if (client.print(bodyStart) != (int)bodyStart.length())
        return false;
    size_t sent = client.write(bin, binLen);
    if (sent != binLen)
        return false;
    if (client.print(bodyEnd) != (int)bodyEnd.length())
        return false;

    // Đợi status 200
    uint32_t start = millis();
    while (client.connected() && millis() - start < 8000)
    {
        while (client.available())
        {
            String line = client.readStringUntil('\n');
            if (line.startsWith("HTTP/1.1 200"))
                return true;
        }
        delay(10);
    }
    return false;
}

bool HttpUploader::postJpeg(const char *url,
                            const char *bearer,
                            const char *deviceId,
                            const uint8_t *data,
                            size_t len,
                            uint32_t tsSec,
                            const char *extraJson)
{
    if (WiFi.status() != WL_CONNECTED)
        return false;

    // Tạo message ký: device.ts.sha256(img)
    String imgHash = sha256Hex(data, len);
    // KHÔNG biết HMAC key ở đây → ta để firmware chuẩn bị sẵn, nhưng để tiện ta đọc từ cfg trong main khi gọi
    // => truyền sẵn signature qua bearer? Không: ta ký ở đây bằng HMAC_KEY truyền bằng extern (khai báo ở main)
    extern const char *__esp32cam_hmac_key; // defined in main.cpp
    String message = String(deviceId) + "." + String(tsSec) + "." + imgHash;
    String signature = hmacSha256Hex(__esp32cam_hmac_key, message);

    String host, path;
    uint16_t port;
    splitUrl(url, host, port, path);

    WiFiClient client;
    if (!client.connect(host.c_str(), port))
    {
        Serial.println("[E] HTTP connect fail");
        return false;
    }

    const String boundary = "----esp32camboundary";
    String bodyStart;
    bodyStart.reserve(512);

    bodyStart = "--" + boundary + "\r\n";
    bodyStart += "Content-Disposition: form-data; name=\"device_id\"\r\n\r\n";
    bodyStart += String(deviceId) + "\r\n";

    bodyStart += "--" + boundary + "\r\n";
    bodyStart += "Content-Disposition: form-data; name=\"ts\"\r\n\r\n";
    bodyStart += String(tsSec) + "\r\n";

    bodyStart += "--" + boundary + "\r\n";
    bodyStart += "Content-Disposition: form-data; name=\"extra\"\r\n\r\n";
    bodyStart += (extraJson ? String(extraJson) : String("{}")) + "\r\n";

    bodyStart += "--" + boundary + "\r\n";
    bodyStart += "Content-Disposition: form-data; name=\"photo\"; filename=\"frame.jpg\"\r\n";
    bodyStart += "Content-Type: image/jpeg\r\n\r\n";

    String bodyEnd = "\r\n--" + boundary + "--\r\n";

    bool ok = sendMultipart(client, host, path, bearer, signature, boundary, bodyStart, data, len, bodyEnd);
    client.stop();
    return ok;
}
