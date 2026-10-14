#include "mafia/json.hpp"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>

namespace mafia::json {

namespace {

constexpr int kMaxDepth = 64;

void appendUtf8(std::string& out, std::uint32_t code) {
    if (code < 0x80) {
        out += static_cast<char>(code);
    } else if (code < 0x800) {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

void dumpString(std::string& out, const std::string& text) {
    out += '"';
    for (const char c : text) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char escaped[8];
                    std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned>(c));
                    out += escaped;
                } else {
                    out += c;  // UTF-8 передаётся как есть
                }
        }
    }
    out += '"';
}

void dumpValue(std::string& out, const Value& value) {
    if (value.isNull()) {
        out += "null";
    } else if (value.isBool()) {
        out += value.asBool() ? "true" : "false";
    } else if (value.isNumber()) {
        const double number = value.asNumber();
        char buffer[32];
        if (std::isfinite(number) && number == std::floor(number) && std::fabs(number) < 1e15) {
            std::snprintf(buffer, sizeof(buffer), "%.0f", number);
        } else if (std::isfinite(number)) {
            std::snprintf(buffer, sizeof(buffer), "%.17g", number);
        } else {
            std::snprintf(buffer, sizeof(buffer), "null");  // в JSON нет NaN и бесконечностей
        }
        out += buffer;
    } else if (value.isString()) {
        dumpString(out, value.asString());
    } else if (value.isArray()) {
        out += '[';
        bool first = true;
        for (const Value& item : value.asArray()) {
            out += first ? "" : ",";
            first = false;
            dumpValue(out, item);
        }
        out += ']';
    } else {
        out += '{';
        bool first = true;
        for (const auto& [key, item] : value.asObject()) {
            out += first ? "" : ",";
            first = false;
            dumpString(out, key);
            out += ':';
            dumpValue(out, item);
        }
        out += '}';
    }
}

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    Value parseDocument() {
        skipSpace();
        Value value = parseValue(0);
        skipSpace();
        if (pos_ != text_.size()) {
            fail("лишние символы после JSON");
        }
        return value;
    }

private:
    [[noreturn]] void fail(const std::string& detail) const { throw ParseError(pos_, detail); }

    bool atEnd() const { return pos_ >= text_.size(); }
    char peek() const { return atEnd() ? '\0' : text_[pos_]; }

    void skipSpace() {
        while (!atEnd() && (peek() == ' ' || peek() == '\t' || peek() == '\n' || peek() == '\r')) {
            ++pos_;
        }
    }

    void expect(std::string_view word) {
        if (text_.substr(pos_, word.size()) != word) {
            fail("ожидалось «" + std::string(word) + "»");
        }
        pos_ += word.size();
    }

    Value parseValue(int depth) {
        if (depth > kMaxDepth) {
            fail("слишком глубокая вложенность");
        }
        switch (peek()) {
            case '{':
                return parseObject(depth);
            case '[':
                return parseArray(depth);
            case '"':
                return Value(parseString());
            case 't':
                expect("true");
                return Value(true);
            case 'f':
                expect("false");
                return Value(false);
            case 'n':
                expect("null");
                return Value(nullptr);
            default:
                if (peek() == '-' || (peek() >= '0' && peek() <= '9')) {
                    return Value(parseNumber());
                }
                fail(atEnd() ? "неожиданный конец JSON" : "неожиданный символ");
        }
    }

    Value parseObject(int depth) {
        ++pos_;  // {
        Object object;
        skipSpace();
        if (peek() == '}') {
            ++pos_;
            return Value(std::move(object));
        }
        for (;;) {
            skipSpace();
            if (peek() != '"') {
                fail("ожидался ключ в кавычках");
            }
            std::string key = parseString();
            skipSpace();
            if (peek() != ':') {
                fail("ожидалось «:»");
            }
            ++pos_;
            skipSpace();
            object.emplace_back(std::move(key), parseValue(depth + 1));
            skipSpace();
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            if (peek() == '}') {
                ++pos_;
                return Value(std::move(object));
            }
            fail("ожидалось «,» или «}»");
        }
    }

    Value parseArray(int depth) {
        ++pos_;  // [
        Array array;
        skipSpace();
        if (peek() == ']') {
            ++pos_;
            return Value(std::move(array));
        }
        for (;;) {
            skipSpace();
            array.push_back(parseValue(depth + 1));
            skipSpace();
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            if (peek() == ']') {
                ++pos_;
                return Value(std::move(array));
            }
            fail("ожидалось «,» или «]»");
        }
    }

    std::uint32_t parseHex4() {
        if (pos_ + 4 > text_.size()) {
            fail("обрезанная последовательность \\u");
        }
        std::uint32_t code = 0;
        const auto [ptr, error] = std::from_chars(text_.data() + pos_, text_.data() + pos_ + 4, code, 16);
        if (error != std::errc{} || ptr != text_.data() + pos_ + 4) {
            fail("неверная последовательность \\u");
        }
        pos_ += 4;
        return code;
    }

    std::string parseString() {
        ++pos_;  // "
        std::string out;
        for (;;) {
            if (atEnd()) {
                fail("незакрытая строка");
            }
            const char c = text_[pos_++];
            if (c == '"') {
                return out;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                fail("управляющий символ внутри строки");
            }
            if (c != '\\') {
                out += c;
                continue;
            }
            if (atEnd()) {
                fail("незакрытая строка");
            }
            const char escaped = text_[pos_++];
            switch (escaped) {
                case '"':
                case '\\':
                case '/':
                    out += escaped;
                    break;
                case 'b':
                    out += '\b';
                    break;
                case 'f':
                    out += '\f';
                    break;
                case 'n':
                    out += '\n';
                    break;
                case 'r':
                    out += '\r';
                    break;
                case 't':
                    out += '\t';
                    break;
                case 'u': {
                    std::uint32_t code = parseHex4();
                    if (code >= 0xD800 && code <= 0xDBFF) {  // суррогатная пара
                        if (text_.substr(pos_, 2) != "\\u") {
                            fail("одинокий суррогат в \\u");
                        }
                        pos_ += 2;
                        const std::uint32_t low = parseHex4();
                        if (low < 0xDC00 || low > 0xDFFF) {
                            fail("неверная суррогатная пара");
                        }
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    } else if (code >= 0xDC00 && code <= 0xDFFF) {
                        fail("одинокий суррогат в \\u");
                    }
                    appendUtf8(out, code);
                    break;
                }
                default:
                    fail("неизвестная escape-последовательность");
            }
        }
    }

    double parseNumber() {
        const std::size_t start = pos_;
        if (peek() == '-') {
            ++pos_;
        }
        if (peek() == '0') {
            ++pos_;
        } else if (peek() >= '1' && peek() <= '9') {
            while (peek() >= '0' && peek() <= '9') {
                ++pos_;
            }
        } else {
            fail("неверное число");
        }
        if (peek() == '.') {
            ++pos_;
            if (!(peek() >= '0' && peek() <= '9')) {
                fail("неверное число");
            }
            while (peek() >= '0' && peek() <= '9') {
                ++pos_;
            }
        }
        if (peek() == 'e' || peek() == 'E') {
            ++pos_;
            if (peek() == '+' || peek() == '-') {
                ++pos_;
            }
            if (!(peek() >= '0' && peek() <= '9')) {
                fail("неверное число");
            }
            while (peek() >= '0' && peek() <= '9') {
                ++pos_;
            }
        }
        const std::string number(text_.substr(start, pos_ - start));
        return std::strtod(number.c_str(), nullptr);
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

}  // namespace

const Value* Value::find(std::string_view key) const {
    if (!isObject()) {
        return nullptr;
    }
    for (const auto& [name, value] : asObject()) {
        if (name == key) {
            return &value;
        }
    }
    return nullptr;
}

std::string Value::dump() const {
    std::string out;
    dumpValue(out, *this);
    return out;
}

ParseError::ParseError(std::size_t offset, const std::string& detail)
    : std::runtime_error("JSON, позиция " + std::to_string(offset) + ": " + detail), offset_(offset) {}

Value parse(std::string_view text) {
    return Parser(text).parseDocument();
}

}  // namespace mafia::json
