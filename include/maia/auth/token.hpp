#pragma once

#include <string>
#include <string_view>
#include <cstdint>
#include <optional>
#include "maia/core/json.hpp"

namespace maia::auth {

struct PlaybackClaims {
    std::string asset_id;
    std::string sub = "anonymous";
    std::uint64_t exp = 0;
    std::string op = "play";
    std::string nonce;
};

class TokenManager {
public:
    explicit TokenManager(std::string secret_key, std::string issuer = "maia-platform");

    [[nodiscard]] std::string create_token(const PlaybackClaims& claims) const;
    [[nodiscard]] std::string create_token(std::string_view asset_id,
                                           std::uint64_t ttl_seconds = 3600,
                                           std::string_view op = "play",
                                           std::string_view sub = "anonymous") const;

    [[nodiscard]] std::optional<PlaybackClaims> verify_token(std::string_view token,
                                                             std::string_view required_asset_id = "",
                                                             std::string_view required_op = "") const;

    [[nodiscard]] const std::string& secret_key() const noexcept { return secret_key_; }
    void set_secret_key(std::string key) { secret_key_ = std::move(key); }

private:
    std::string secret_key_;
    std::string issuer_;
};

} // namespace maia::auth

