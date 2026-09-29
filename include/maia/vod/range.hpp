#pragma once
#include <cstdint>
#include <optional>
#include <string_view>

namespace maia::vod {
struct ByteRange {
    std::uint64_t first{};
    std::uint64_t last{};
    [[nodiscard]] std::uint64_t length() const noexcept { return last - first + 1; }
};

// Parses a single RFC-style bytes range against a known object size.
// Multi-range responses are intentionally deferred from v0.1.
[[nodiscard]] std::optional<ByteRange> parseByteRange(
    std::string_view header,
    std::uint64_t objectSize) noexcept;
}
