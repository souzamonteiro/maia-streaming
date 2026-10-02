#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <variant>
#include <optional>
#include <sstream>
#include <cstdint>
#include <iomanip>
#include <stdexcept>

namespace maia::core {

class Json {
public:
    enum class Type {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json>;

private:
    Type type_{Type::Null};
    std::variant<std::monostate, bool, double, std::string, Array, Object> value_;

public:
    Json() : type_(Type::Null), value_(std::monostate{}) {}
    Json(std::nullptr_t) : type_(Type::Null), value_(std::monostate{}) {}
    Json(bool b) : type_(Type::Bool), value_(b) {}
    Json(int n) : type_(Type::Number), value_(static_cast<double>(n)) {}
    Json(long n) : type_(Type::Number), value_(static_cast<double>(n)) {}
    Json(long long n) : type_(Type::Number), value_(static_cast<double>(n)) {}
    Json(unsigned int n) : type_(Type::Number), value_(static_cast<double>(n)) {}
    Json(unsigned long n) : type_(Type::Number), value_(static_cast<double>(n)) {}
    Json(unsigned long long n) : type_(Type::Number), value_(static_cast<double>(n)) {}
    Json(double n) : type_(Type::Number), value_(n) {}
    Json(const char* s) : type_(Type::String), value_(std::string(s ? s : "")) {}
    Json(std::string s) : type_(Type::String), value_(std::move(s)) {}
    Json(std::string_view s) : type_(Type::String), value_(std::string(s)) {}
    Json(Array arr) : type_(Type::Array), value_(std::move(arr)) {}
    Json(Object obj) : type_(Type::Object), value_(std::move(obj)) {}

    static Json object() { return Json(Object{}); }
    static Json array() { return Json(Array{}); }

    [[nodiscard]] Type type() const noexcept { return type_; }
    [[nodiscard]] bool is_null() const noexcept { return type_ == Type::Null; }
    [[nodiscard]] bool is_bool() const noexcept { return type_ == Type::Bool; }
    [[nodiscard]] bool is_number() const noexcept { return type_ == Type::Number; }
    [[nodiscard]] bool is_string() const noexcept { return type_ == Type::String; }
    [[nodiscard]] bool is_array() const noexcept { return type_ == Type::Array; }
    [[nodiscard]] bool is_object() const noexcept { return type_ == Type::Object; }

    [[nodiscard]] bool as_bool(bool def = false) const noexcept {
        if (is_bool()) return std::get<bool>(value_);
        return def;
    }

    [[nodiscard]] int64_t as_int64(int64_t def = 0) const noexcept {
        if (is_number()) return static_cast<int64_t>(std::get<double>(value_));
        return def;
    }

    [[nodiscard]] uint64_t as_uint64(uint64_t def = 0) const noexcept {
        if (is_number()) return static_cast<uint64_t>(std::get<double>(value_));
        return def;
    }

    [[nodiscard]] int as_int(int def = 0) const noexcept {
        return static_cast<int>(as_int64(def));
    }

    [[nodiscard]] double as_double(double def = 0.0) const noexcept {
        if (is_number()) return std::get<double>(value_);
        return def;
    }

    [[nodiscard]] const std::string& as_string() const {
        static const std::string empty;
        if (is_string()) return std::get<std::string>(value_);
        return empty;
    }

    [[nodiscard]] std::string as_string(std::string_view def) const {
        if (is_string()) return std::get<std::string>(value_);
        return std::string(def);
    }

    [[nodiscard]] const Array& as_array() const {
        static const Array empty;
        if (is_array()) return std::get<Array>(value_);
        return empty;
    }

    [[nodiscard]] Array& as_array() {
        if (!is_array()) {
            type_ = Type::Array;
            value_ = Array{};
        }
        return std::get<Array>(value_);
    }

    [[nodiscard]] const Object& as_object() const {
        static const Object empty;
        if (is_object()) return std::get<Object>(value_);
        return empty;
    }

    [[nodiscard]] Object& as_object() {
        if (!is_object()) {
            type_ = Type::Object;
            value_ = Object{};
        }
        return std::get<Object>(value_);
    }

    [[nodiscard]] bool contains(const std::string& key) const noexcept {
        if (!is_object()) return false;
        const auto& obj = std::get<Object>(value_);
        return obj.find(key) != obj.end();
    }

    [[nodiscard]] Json get(const std::string& key, Json def = Json()) const {
        if (!is_object()) return def;
        const auto& obj = std::get<Object>(value_);
        auto it = obj.find(key);
        if (it != obj.end()) return it->second;
        return def;
    }

    Json& operator[](const std::string& key) {
        return as_object()[key];
    }

    Json& operator[](std::size_t index) {
        auto& arr = as_array();
        if (index >= arr.size()) arr.resize(index + 1);
        return arr[index];
    }

    void push_back(Json val) {
        as_array().push_back(std::move(val));
    }

    [[nodiscard]] std::string dump(int indent = -1) const {
        std::ostringstream ss;
        dump_internal(ss, indent, 0);
        return ss.str();
    }

    static std::optional<Json> parse(std::string_view str) {
        std::size_t pos = 0;
        skip_ws(str, pos);
        if (pos >= str.size()) return std::nullopt;
        auto res = parse_value(str, pos);
        skip_ws(str, pos);
        if (!res || pos != str.size()) return std::nullopt;
        return res;
    }

private:
    static void skip_ws(std::string_view s, std::size_t& pos) noexcept {
        while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\r' || s[pos] == '\n')) {
            ++pos;
        }
    }

    static std::optional<Json> parse_value(std::string_view s, std::size_t& pos) {
        skip_ws(s, pos);
        if (pos >= s.size()) return std::nullopt;

        char c = s[pos];
        if (c == 'n') {
            if (s.substr(pos, 4) == "null") {
                pos += 4;
                return Json();
            }
            return std::nullopt;
        } else if (c == 't') {
            if (s.substr(pos, 4) == "true") {
                pos += 4;
                return Json(true);
            }
            return std::nullopt;
        } else if (c == 'f') {
            if (s.substr(pos, 5) == "false") {
                pos += 5;
                return Json(false);
            }
            return std::nullopt;
        } else if (c == '"') {
            return parse_string(s, pos);
        } else if (c == '[') {
            return parse_array(s, pos);
        } else if (c == '{') {
            return parse_object(s, pos);
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            return parse_number(s, pos);
        }
        return std::nullopt;
    }

    static std::optional<Json> parse_string(std::string_view s, std::size_t& pos) {
        if (pos >= s.size() || s[pos] != '"') return std::nullopt;
        ++pos; // skip '"'
        std::string res;
        while (pos < s.size()) {
            char c = s[pos++];
            if (c == '"') {
                return Json(std::move(res));
            } else if (c == '\\') {
                if (pos >= s.size()) return std::nullopt;
                char esc = s[pos++];
                switch (esc) {
                    case '"': res.push_back('"'); break;
                    case '\\': res.push_back('\\'); break;
                    case '/': res.push_back('/'); break;
                    case 'b': res.push_back('\b'); break;
                    case 'f': res.push_back('\f'); break;
                    case 'n': res.push_back('\n'); break;
                    case 'r': res.push_back('\r'); break;
                    case 't': res.push_back('\t'); break;
                    case 'u': {
                        if (pos + 4 > s.size()) return std::nullopt;
                        std::string hex(s.substr(pos, 4));
                        pos += 4;
                        try {
                            unsigned long codepoint = std::stoul(hex, nullptr, 16);
                            if (codepoint < 0x80) {
                                res.push_back(static_cast<char>(codepoint));
                            } else if (codepoint < 0x800) {
                                res.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
                                res.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                            } else {
                                res.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
                                res.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                                res.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                            }
                        } catch (...) {
                            res.push_back('?');
                        }
                        break;
                    }
                    default: res.push_back(esc); break;
                }
            } else {
                res.push_back(c);
            }
        }
        return std::nullopt;
    }

    static std::optional<Json> parse_number(std::string_view s, std::size_t& pos) {
        std::size_t start = pos;
        if (pos < s.size() && s[pos] == '-') ++pos;
        while (pos < s.size() && (s[pos] >= '0' && s[pos] <= '9')) ++pos;
        if (pos < s.size() && s[pos] == '.') {
            ++pos;
            while (pos < s.size() && (s[pos] >= '0' && s[pos] <= '9')) ++pos;
        }
        if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
            ++pos;
            if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) ++pos;
            while (pos < s.size() && (s[pos] >= '0' && s[pos] <= '9')) ++pos;
        }
        std::string num_str(s.substr(start, pos - start));
        try {
            double val = std::stod(num_str);
            return Json(val);
        } catch (...) {
            return std::nullopt;
        }
    }

    static std::optional<Json> parse_array(std::string_view s, std::size_t& pos) {
        if (pos >= s.size() || s[pos] != '[') return std::nullopt;
        ++pos; // skip '['
        Array arr;
        skip_ws(s, pos);
        if (pos < s.size() && s[pos] == ']') {
            ++pos;
            return Json(std::move(arr));
        }

        while (pos < s.size()) {
            auto val = parse_value(s, pos);
            if (!val) return std::nullopt;
            arr.push_back(std::move(*val));

            skip_ws(s, pos);
            if (pos >= s.size()) return std::nullopt;
            if (s[pos] == ']') {
                ++pos;
                return Json(std::move(arr));
            } else if (s[pos] == ',') {
                ++pos;
                skip_ws(s, pos);
            } else {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    static std::optional<Json> parse_object(std::string_view s, std::size_t& pos) {
        if (pos >= s.size() || s[pos] != '{') return std::nullopt;
        ++pos; // skip '{'
        Object obj;
        skip_ws(s, pos);
        if (pos < s.size() && s[pos] == '}') {
            ++pos;
            return Json(std::move(obj));
        }

        while (pos < s.size()) {
            skip_ws(s, pos);
            if (pos >= s.size() || s[pos] != '"') return std::nullopt;
            auto key_val = parse_string(s, pos);
            if (!key_val || !key_val->is_string()) return std::nullopt;
            std::string key = key_val->as_string();

            skip_ws(s, pos);
            if (pos >= s.size() || s[pos] != ':') return std::nullopt;
            ++pos; // skip ':'

            auto val = parse_value(s, pos);
            if (!val) return std::nullopt;
            obj.emplace(std::move(key), std::move(*val));

            skip_ws(s, pos);
            if (pos >= s.size()) return std::nullopt;
            if (s[pos] == '}') {
                ++pos;
                return Json(std::move(obj));
            } else if (s[pos] == ',') {
                ++pos;
                skip_ws(s, pos);
            } else {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    void dump_internal(std::ostringstream& ss, int indent, int depth) const {
        switch (type_) {
            case Type::Null: ss << "null"; break;
            case Type::Bool: ss << (std::get<bool>(value_) ? "true" : "false"); break;
            case Type::Number: {
                double val = std::get<double>(value_);
                if (val == static_cast<int64_t>(val)) {
                    ss << static_cast<int64_t>(val);
                } else {
                    ss << std::setprecision(10) << val;
                }
                break;
            }
            case Type::String: {
                ss << '"';
                for (char c : std::get<std::string>(value_)) {
                    switch (c) {
                        case '"': ss << "\\\""; break;
                        case '\\': ss << "\\\\"; break;
                        case '\b': ss << "\\b"; break;
                        case '\f': ss << "\\f"; break;
                        case '\n': ss << "\\n"; break;
                        case '\r': ss << "\\r"; break;
                        case '\t': ss << "\\t"; break;
                        default:
                            if (static_cast<unsigned char>(c) < 0x20) {
                                ss << "\\u" << std::hex << std::setfill('0') << std::setw(4) << static_cast<int>(c) << std::dec;
                            } else {
                                ss << c;
                            }
                            break;
                    }
                }
                ss << '"';
                break;
            }
            case Type::Array: {
                const auto& arr = std::get<Array>(value_);
                if (arr.empty()) {
                    ss << "[]";
                    break;
                }
                ss << '[';
                if (indent >= 0) ss << '\n';
                for (std::size_t i = 0; i < arr.size(); ++i) {
                    if (indent >= 0) ss << std::string((depth + 1) * indent, ' ');
                    arr[i].dump_internal(ss, indent, depth + 1);
                    if (i + 1 < arr.size()) ss << ',';
                    if (indent >= 0) ss << '\n';
                }
                if (indent >= 0) ss << std::string(depth * indent, ' ');
                ss << ']';
                break;
            }
            case Type::Object: {
                const auto& obj = std::get<Object>(value_);
                if (obj.empty()) {
                    ss << "{}";
                    break;
                }
                ss << '{';
                if (indent >= 0) ss << '\n';
                std::size_t i = 0;
                for (const auto& [k, v] : obj) {
                    if (indent >= 0) ss << std::string((depth + 1) * indent, ' ');
                    ss << '"' << k << "\":";
                    if (indent >= 0) ss << ' ';
                    v.dump_internal(ss, indent, depth + 1);
                    if (++i < obj.size()) ss << ',';
                    if (indent >= 0) ss << '\n';
                }
                if (indent >= 0) ss << std::string(depth * indent, ' ');
                ss << '}';
                break;
            }
        }
    }
};

} // namespace maia::core
