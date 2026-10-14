#pragma once

// Небольшой JSON без сторонних библиотек: значение, разбор и сериализация.
// Нужен для запросов к API языковой модели и разбора её ответов.

#include <cstddef>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace mafia::json {

class Value;
using Array = std::vector<Value>;
using Member = std::pair<std::string, Value>;
using Object = std::vector<Member>;  // порядок ключей сохраняется — вывод детерминирован

class Value {
public:
    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool value) : data_(value) {}
    Value(int value) : data_(static_cast<double>(value)) {}
    Value(long long value) : data_(static_cast<double>(value)) {}
    Value(double value) : data_(value) {}
    Value(const char* value) : data_(std::string(value)) {}
    Value(std::string value) : data_(std::move(value)) {}
    Value(std::string_view value) : data_(std::string(value)) {}
    Value(json::Array value) : data_(std::move(value)) {}
    Value(json::Object value) : data_(std::move(value)) {}

    bool isNull() const noexcept { return std::holds_alternative<std::nullptr_t>(data_); }
    bool isBool() const noexcept { return std::holds_alternative<bool>(data_); }
    bool isNumber() const noexcept { return std::holds_alternative<double>(data_); }
    bool isString() const noexcept { return std::holds_alternative<std::string>(data_); }
    bool isArray() const noexcept { return std::holds_alternative<json::Array>(data_); }
    bool isObject() const noexcept { return std::holds_alternative<json::Object>(data_); }

    bool asBool() const { return std::get<bool>(data_); }
    double asNumber() const { return std::get<double>(data_); }
    const std::string& asString() const { return std::get<std::string>(data_); }
    const json::Array& asArray() const { return std::get<json::Array>(data_); }
    const json::Object& asObject() const { return std::get<json::Object>(data_); }

    // Поле объекта; nullptr, если это не объект или поля нет.
    const Value* find(std::string_view key) const;

    // Компактная запись: экранирует кавычки, обратную косую черту и управляющие символы.
    std::string dump() const;

    bool operator==(const Value&) const = default;

private:
    std::variant<std::nullptr_t, bool, double, std::string, json::Array, json::Object> data_;
};

class ParseError : public std::runtime_error {
public:
    ParseError(std::size_t offset, const std::string& detail);
    std::size_t offset() const noexcept { return offset_; }

private:
    std::size_t offset_;
};

// Разбирает ровно один JSON-документ (пробелы по краям допустимы). Бросает ParseError.
Value parse(std::string_view text);

}  // namespace mafia::json
