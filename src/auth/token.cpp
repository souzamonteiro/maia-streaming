#include "maia/auth/token.hpp"
#include "maia/core/utils.hpp"
#include <iostream>

namespace maia::auth {

TokenManager::TokenManager(std::string secret_key, std::string issuer)
    : secret_key_(std::move(secret_key)), issuer_(std::move(issuer)) {
    if (secret_key_.empty()) {
        secret_key_ = "maia-default-development-secret-key-do-not-use-in-prod";
    }
}

std::string TokenManager::create_token(const PlaybackClaims& claims) const {
    core::Json header = core::Json::object();
    header["alg"] = "HS256";
    header["typ"] = "JWT";

    core::Json payload = core::Json::object();
    payload["iss"] = issuer_;
    payload["asset_id"] = claims.asset_id;
    payload["sub"] = claims.sub;
    payload["exp"] = claims.exp;
    payload["op"] = claims.op;
    payload["nonce"] = claims.nonce.empty() ? core::generate_id("n") : claims.nonce;

    std::string header_b64 = core::base64url_encode(header.dump());
    std::string payload_b64 = core::base64url_encode(payload.dump());

    std::string to_sign = header_b64 + "." + payload_b64;
    std::string sig_b64 = core::hmac_sha256_base64url(secret_key_, to_sign);

    return to_sign + "." + sig_b64;
}

std::string TokenManager::create_token(std::string_view asset_id,
                                       std::uint64_t ttl_seconds,
                                       std::string_view op,
                                       std::string_view sub) const {
    PlaybackClaims claims;
    claims.asset_id = std::string(asset_id);
    claims.exp = core::now_seconds() + ttl_seconds;
    claims.op = std::string(op);
    claims.sub = std::string(sub);
    claims.nonce = core::generate_id("n");
    return create_token(claims);
}

std::optional<PlaybackClaims> TokenManager::verify_token(std::string_view token,
                                                         std::string_view required_asset_id,
                                                         std::string_view required_op) const {
    auto parts = core::split(token, '.');
    if (parts.size() != 3) {
        return std::nullopt;
    }

    std::string to_verify = parts[0] + "." + parts[1];
    std::string expected_sig = core::hmac_sha256_base64url(secret_key_, to_verify);
    if (expected_sig != parts[2]) {
        return std::nullopt;
    }

    auto payload_bytes = core::base64url_decode(parts[1]);
    if (!payload_bytes) {
        return std::nullopt;
    }

    std::string payload_str(payload_bytes->begin(), payload_bytes->end());
    auto json_opt = core::Json::parse(payload_str);
    if (!json_opt || !json_opt->is_object()) {
        return std::nullopt;
    }

    const auto& json = *json_opt;
    PlaybackClaims claims;
    claims.asset_id = json.get("asset_id").as_string();
    claims.sub = json.get("sub").as_string("anonymous");
    claims.exp = json.get("exp").as_uint64();
    claims.op = json.get("op").as_string("play");
    claims.nonce = json.get("nonce").as_string();

    // Check expiration (RFC 7519 §4.1.4: current time must be before exp)
    if (claims.exp > 0 && claims.exp <= core::now_seconds()) {
        return std::nullopt;
    }

    // Check required asset_id if specified
    if (!required_asset_id.empty() && claims.asset_id != "*" && claims.asset_id != required_asset_id) {
        return std::nullopt;
    }

    // Check required operation if specified
    if (!required_op.empty() && claims.op != "admin" && claims.op != required_op) {
        return std::nullopt;
    }

    return claims;
}

} // namespace maia::auth

