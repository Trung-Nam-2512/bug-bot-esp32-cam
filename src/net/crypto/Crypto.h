#pragma once
#include <Arduino.h>

// sha256(buffer) -> hex lowercase
String sha256Hex(const uint8_t *data, size_t len);

// hmacSha256(key, message) -> hex lowercase
String hmacSha256Hex(const char *key, const String &message);
