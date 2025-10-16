#include "Crypto.h"
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>

String sha256Hex(const uint8_t *data, size_t len)
{
    unsigned char out[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts_ret(&ctx, 0); // 0 = SHA256, 1 = SHA224
    mbedtls_sha256_update_ret(&ctx, data, len);
    mbedtls_sha256_finish_ret(&ctx, out);
    mbedtls_sha256_free(&ctx);

    static const char *H = "0123456789abcdef";
    String hex;
    hex.reserve(64);
    for (int i = 0; i < 32; ++i)
    {
        hex += H[out[i] >> 4];
        hex += H[out[i] & 0xF];
    }
    return hex;
}

String hmacSha256Hex(const char *key, const String &message)
{
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, info, 1);
    mbedtls_md_hmac_starts(&ctx, reinterpret_cast<const unsigned char *>(key), strlen(key));
    mbedtls_md_hmac_update(&ctx, reinterpret_cast<const unsigned char *>(message.c_str()), message.length());
    unsigned char out[32];
    mbedtls_md_hmac_finish(&ctx, out);
    mbedtls_md_free(&ctx);

    static const char *H = "0123456789abcdef";
    String hex;
    hex.reserve(64);
    for (int i = 0; i < 32; ++i)
    {
        hex += H[out[i] >> 4];
        hex += H[out[i] & 0xF];
    }
    return hex;
}
