#include "maia/core/utils.hpp"
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <random>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <algorithm>
#include <cctype>

namespace maia::core {

std::uint64_t now_seconds() noexcept {
    const auto now = std::chrono::system_clock::now();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
}

std::uint64_t now_milliseconds() noexcept {
    const auto now = std::chrono::system_clock::now();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
}

std::string iso8601_now() {
    return iso8601_format(now_seconds());
}

std::string iso8601_format(std::uint64_t seconds_since_epoch) {
    std::time_t t = static_cast<std::time_t>(seconds_since_epoch);
    std::tm tm_buf{};
    gmtime_r(&t, &tm_buf);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return std::string(buf);
}

std::string generate_id(std::string_view prefix) {
    // 10-char timestamp (Crockford-style base32) + 16-char random
    static constexpr char crockford_chars[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    thread_local std::random_device rd;
    thread_local std::mt19937_64 gen(rd());
    thread_local std::uniform_int_distribution<std::size_t> dist(0, 31);

    auto ms = now_milliseconds();
    std::string id;
    id.reserve(prefix.size() + 26);
    if (!prefix.empty()) {
        id.append(prefix);
        if (!prefix.ends_with('_') && !prefix.ends_with('-')) {
            id.push_back('_');
        }
    }

    // 10 chars of timestamp (high ms first)
    char time_part[10];
    for (int i = 9; i >= 0; --i) {
        time_part[i] = crockford_chars[ms & 0x1F];
        ms >>= 5;
    }
    id.append(time_part, 10);

    // 16 chars of randomness
    for (int i = 0; i < 16; ++i) {
        id.push_back(crockford_chars[dist(gen)]);
    }

    return id;
}

std::string trim(std::string_view str) {
    auto start = str.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) return "";
    auto end = str.find_last_not_of(" \t\r\n");
    return std::string(str.substr(start, end - start + 1));
}

std::vector<std::string> split(std::string_view str, char delimiter) {
    std::vector<std::string> tokens;
    std::size_t start = 0;
    while (start < str.size()) {
        auto end = str.find(delimiter, start);
        if (end == std::string_view::npos) {
            tokens.emplace_back(str.substr(start));
            break;
        }
        tokens.emplace_back(str.substr(start, end - start));
        start = end + 1;
    }
    return tokens;
}

std::string to_lower(std::string_view str) {
    std::string result;
    result.reserve(str.size());
    for (char c : str) {
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return result;
}

bool iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

std::string url_decode(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            auto from_hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int h1 = from_hex(in[i + 1]);
            int h2 = from_hex(in[i + 2]);
            if (h1 >= 0 && h2 >= 0) {
                out.push_back(static_cast<char>((h1 << 4) | h2));
                i += 2;
                continue;
            }
        } else if (in[i] == '+') {
            out.push_back(' ');
            continue;
        }
        out.push_back(in[i]);
    }
    return out;
}

std::string url_encode(std::string_view in) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (char c : in) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::setw(2) << static_cast<int>(static_cast<unsigned char>(c));
        }
    }
    return escaped.str();
}

static constexpr char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const std::uint8_t* data, std::size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (std::size_t i = 0; i < len; i += 3) {
        std::uint32_t val = (static_cast<std::uint32_t>(data[i]) << 16);
        if (i + 1 < len) val |= (static_cast<std::uint32_t>(data[i + 1]) << 8);
        if (i + 2 < len) val |= static_cast<std::uint32_t>(data[i + 2]);

        out.push_back(b64_table[(val >> 18) & 0x3F]);
        out.push_back(b64_table[(val >> 12) & 0x3F]);
        out.push_back((i + 1 < len) ? b64_table[(val >> 6) & 0x3F] : '=');
        out.push_back((i + 2 < len) ? b64_table[val & 0x3F] : '=');
    }
    return out;
}

std::string base64_encode(std::string_view input) {
    return base64_encode(reinterpret_cast<const std::uint8_t*>(input.data()), input.size());
}

std::optional<std::vector<std::uint8_t>> base64_decode(std::string_view input) {
    auto decode_char = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+' || c == '-') return 62;
        if (c == '/' || c == '_') return 63;
        return -1;
    };

    std::vector<std::uint8_t> out;
    out.reserve((input.size() * 3) / 4);

    std::uint32_t buf = 0;
    int bits = 0;
    for (char c : input) {
        if (c == '=' || std::isspace(static_cast<unsigned char>(c))) continue;
        int val = decode_char(c);
        if (val < 0) return std::nullopt;
        buf = (buf << 6) | static_cast<std::uint32_t>(val);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((buf >> bits) & 0xFF));
        }
    }
    return out;
}

std::string base64url_encode(const std::uint8_t* data, std::size_t len) {
    std::string b64 = base64_encode(data, len);
    std::string res;
    res.reserve(b64.size());
    for (char c : b64) {
        if (c == '+') res.push_back('-');
        else if (c == '/') res.push_back('_');
        else if (c != '=') res.push_back(c);
    }
    return res;
}

std::string base64url_encode(std::string_view input) {
    return base64url_encode(reinterpret_cast<const std::uint8_t*>(input.data()), input.size());
}

std::optional<std::vector<std::uint8_t>> base64url_decode(std::string_view input) {
    std::string b64(input);
    for (char& c : b64) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }
    while (b64.size() % 4 != 0) {
        b64.push_back('=');
    }
    return base64_decode(b64);
}

std::string sha256_hex(const std::uint8_t* data, std::size_t len) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(data, len, hash);
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        ss << std::setw(2) << static_cast<int>(hash[i]);
    }
    return ss.str();
}

std::string sha256_hex(std::string_view input) {
    return sha256_hex(reinterpret_cast<const std::uint8_t*>(input.data()), input.size());
}

std::vector<std::uint8_t> hmac_sha256(std::string_view key, std::string_view message) {
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int md_len = 0;
    HMAC(EVP_sha256(),
         key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(message.data()), message.size(),
         md, &md_len);
    return std::vector<std::uint8_t>(md, md + md_len);
}

std::string hmac_sha256_base64url(std::string_view key, std::string_view message) {
    auto hmac = hmac_sha256(key, message);
    return base64url_encode(hmac.data(), hmac.size());
}

std::string mime_type_from_filename(std::string_view filename) {
    auto dot = filename.find_last_of('.');
    if (dot == std::string_view::npos) return "application/octet-stream";
    std::string ext = to_lower(filename.substr(dot));

    if (ext == ".mp4") return "video/mp4";
    if (ext == ".webm") return "video/webm";
    if (ext == ".mkv") return "video/x-matroska";
    if (ext == ".mov") return "video/quicktime";
    if (ext == ".avi") return "video/x-msvideo";
    if (ext == ".m3u8") return "application/vnd.apple.mpegurl";
    if (ext == ".ts") return "video/MP2T";
    if (ext == ".m4s") return "video/iso.segment";
    if (ext == ".mp3") return "audio/mpeg";
    if (ext == ".wav") return "audio/wav";
    if (ext == ".ogg" || ext == ".oga") return "audio/ogg";
    if (ext == ".opus") return "audio/opus";
    if (ext == ".flac") return "audio/flac";
    if (ext == ".aac") return "audio/aac";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".png") return "image/png";
    if (ext == ".webp") return "image/webp";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".json") return "application/json";
    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".js" || ext == ".mjs") return "application/javascript; charset=utf-8";
    if (ext == ".txt") return "text/plain; charset=utf-8";

    return "application/octet-stream";
}

std::string sanitize_filename(std::string_view filename) {
    std::string clean;
    clean.reserve(filename.size());
    for (char c : filename) {
        if (c == '/' || c == '\\' || c == '\0' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            clean.push_back('_');
        } else {
            clean.push_back(c);
        }
    }
    if (clean.empty() || clean == "." || clean == "..") {
        return "unnamed_media";
    }
    return clean;
}

} // namespace maia::core

