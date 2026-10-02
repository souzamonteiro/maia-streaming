#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <chrono>
#include <optional>

namespace maia::core {

// Time utilities
[[nodiscard]] std::uint64_t now_seconds() noexcept;
[[nodiscard]] std::uint64_t now_milliseconds() noexcept;
[[nodiscard]] std::string iso8601_now();
[[nodiscard]] std::string iso8601_format(std::uint64_t seconds_since_epoch);

// Unique identifier generator (alphanumeric ULID-compatible style)
[[nodiscard]] std::string generate_id(std::string_view prefix = "");

// String utilities
[[nodiscard]] std::string trim(std::string_view str);
[[nodiscard]] std::vector<std::string> split(std::string_view str, char delimiter);
[[nodiscard]] std::string to_lower(std::string_view str);
[[nodiscard]] bool iequals(std::string_view a, std::string_view b) noexcept;
[[nodiscard]] std::string url_decode(std::string_view in);
[[nodiscard]] std::string url_encode(std::string_view in);

// Base64 & Base64URL
[[nodiscard]] std::string base64_encode(const std::uint8_t* data, std::size_t len);
[[nodiscard]] std::string base64_encode(std::string_view input);
[[nodiscard]] std::optional<std::vector<std::uint8_t>> base64_decode(std::string_view input);
[[nodiscard]] std::string base64url_encode(const std::uint8_t* data, std::size_t len);
[[nodiscard]] std::string base64url_encode(std::string_view input);
[[nodiscard]] std::optional<std::vector<std::uint8_t>> base64url_decode(std::string_view input);

// Crypto helpers using OpenSSL
[[nodiscard]] std::string sha256_hex(const std::uint8_t* data, std::size_t len);
[[nodiscard]] std::string sha256_hex(std::string_view input);
[[nodiscard]] std::vector<std::uint8_t> hmac_sha256(std::string_view key, std::string_view message);
[[nodiscard]] std::string hmac_sha256_base64url(std::string_view key, std::string_view message);

// Media and MIME utilities
[[nodiscard]] std::string mime_type_from_filename(std::string_view filename);
[[nodiscard]] std::string sanitize_filename(std::string_view filename);

} // namespace maia::core

