#pragma once
#include <memory>
#include <stdexcept>
#include <string>
#ifdef __SWITCH__
#include <switch.h>
#elif defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
#else
#include <openssl/evp.h>
#endif

namespace moader_ui {
class Sha256 {
#ifdef __SWITCH__
    Sha256Context ctx;
#elif defined(__APPLE__)
    CC_SHA256_CTX ctx;
#else
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx{EVP_MD_CTX_new(), EVP_MD_CTX_free};
#endif
  public:
    Sha256() {
#ifdef __SWITCH__
        sha256ContextCreate(&ctx);
#elif defined(__APPLE__)
        CC_SHA256_Init(&ctx);
#else
        if (!ctx || !EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr))
            throw std::runtime_error("SHA-256 is unavailable.");
#endif
    }
    void update(const char* data, size_t n) {
#ifdef __SWITCH__
        sha256ContextUpdate(&ctx, data, n);
#elif defined(__APPLE__)
        CC_SHA256_Update(&ctx, data, static_cast<CC_LONG>(n));
#else
        if (!EVP_DigestUpdate(ctx.get(), data, n))
            throw std::runtime_error("Could not verify download.");
#endif
    }
    std::string finish() {
        unsigned char digest[32];
#ifdef __SWITCH__
        sha256ContextGetHash(&ctx, digest);
#elif defined(__APPLE__)
        CC_SHA256_Final(digest, &ctx);
#else
        if (!EVP_DigestFinal_ex(ctx.get(), digest, nullptr))
            throw std::runtime_error("Could not verify download.");
#endif
        constexpr char hex[] = "0123456789abcdef";
        std::string out;
        for (auto b : digest) {
            out.push_back(hex[b >> 4]);
            out.push_back(hex[b & 15]);
        }
        return out;
    }
};
} // namespace moader_ui
