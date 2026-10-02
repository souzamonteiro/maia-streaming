#include "maia/auth/token.hpp"
#include "maia/core/utils.hpp"
#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    using maia::auth::TokenManager;

    TokenManager tm("super-secret-key-12345");

    // 1. Basic token creation and verification
    std::string asset_id = "asset-vid-42";
    std::string token = tm.create_token(asset_id, 3600, "play", "subscriber-99");
    assert(!token.empty());
    assert(token.find('.') != std::string::npos);

    auto claims = tm.verify_token(token, asset_id, "play");
    assert(claims.has_value());
    assert(claims->asset_id == asset_id);
    assert(claims->op == "play");
    assert(claims->sub == "subscriber-99");
    assert(claims->exp > maia::core::now_seconds());

    // 2. Mismatched asset_id
    auto bad_asset = tm.verify_token(token, "different-asset", "play");
    assert(!bad_asset.has_value());

    // 3. Mismatched operation
    auto bad_op = tm.verify_token(token, asset_id, "download");
    assert(!bad_op.has_value());

    // 4. Token verification without strict checks
    auto any_claims = tm.verify_token(token);
    assert(any_claims.has_value());
    assert(any_claims->asset_id == asset_id);

    // 5. Tampered signature
    std::string tampered = token + "xyz";
    assert(!tm.verify_token(tampered, asset_id, "play").has_value());

    auto dot = token.find('.');
    std::string bad_payload = "dGFtcGVyZWQ." + token.substr(dot + 1);
    assert(!tm.verify_token(bad_payload, asset_id, "play").has_value());

    // 6. Token with different key
    TokenManager wrong_tm("other-secret-key-abcde");
    assert(!wrong_tm.verify_token(token, asset_id, "play").has_value());

    // 7. Expired token
    std::string exp_token = tm.create_token(asset_id, 0, "play", "user");
    assert(!tm.verify_token(exp_token, asset_id, "play").has_value());

    std::string exp_token_sleep = tm.create_token(asset_id, 1, "play", "user");
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    assert(!tm.verify_token(exp_token_sleep, asset_id, "play").has_value());

    // 8. Malformed tokens
    assert(!tm.verify_token("").has_value());
    assert(!tm.verify_token("invalid_token_without_dot").has_value());
    assert(!tm.verify_token("a.b.c").has_value());

    std::cout << "[Test PASS] TokenManager authentication & capability tests\n";
    return 0;
}
