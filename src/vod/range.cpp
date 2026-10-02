#include "maia/vod/range.hpp"
#include <charconv>
#include <limits>

namespace maia::vod {
namespace {
std::optional<std::uint64_t> parseUint(std::string_view value) noexcept {
    if (value.empty()) return std::nullopt;
    std::uint64_t out{};
    const auto result = std::from_chars(value.data(), value.data() + value.size(), out);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) return std::nullopt;
    return out;
}
}

std::optional<ByteRange> parseByteRange(std::string_view header, std::uint64_t size) noexcept {
    constexpr std::string_view prefix = "bytes=";
    if (size == 0 || !header.starts_with(prefix)) return std::nullopt;
    auto spec = header.substr(prefix.size());
    if (spec.find(',') != std::string_view::npos) return std::nullopt;
    auto dash = spec.find('-');
    if (dash == std::string_view::npos) return std::nullopt;
    auto left = spec.substr(0, dash);
    auto right = spec.substr(dash + 1);

    if (left.empty()) {
        auto suffix = parseUint(right);
        if (!suffix || *suffix == 0) return std::nullopt;
        auto count = *suffix > size ? size : *suffix;
        return ByteRange{size - count, size - 1};
    }

    auto first = parseUint(left);
    if (!first || *first >= size) return std::nullopt;
    if (right.empty()) return ByteRange{*first, size - 1};

    auto last = parseUint(right);
    if (!last || *last < *first) return std::nullopt;
    if (*last >= size) *last = size - 1;
    return ByteRange{*first, *last};
}

std::string formatContentRange(const ByteRange& range, std::uint64_t total_size) {
    return "bytes " + std::to_string(range.first) + "-" + std::to_string(range.last) + "/" + std::to_string(total_size);
}

std::string formatContentRangeUnsatisfiable(std::uint64_t total_size) {
    return "bytes */" + std::to_string(total_size);
}

} // namespace maia::vod
