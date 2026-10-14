#pragma once

// Небольшой парсер подмножества YAML без сторонних библиотек.
//
// Поддерживается: блочные словари («ключ: значение») и списки («- элемент»)
// с отступами пробелами, скаляры без кавычек и в одинарных/двойных кавычках,
// однострочные списки [a, b, c], комментарии «#», маркер начала документа «---».
// Не поддерживается (даёт понятную ошибку): табуляция в отступах, якоря и
// ссылки (&, *), теги (!), многострочные блоки (|, >), словари {…}, несколько
// документов в одном файле.

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mafia::yaml {

struct Entry;

struct Node {
    enum class Kind { Scalar, Mapping, Sequence };

    Kind kind = Kind::Scalar;
    int line = 0;  // номер строки, с которой начинается узел (с 1)
    std::string scalar;
    bool quoted = false;          // скаляр был в кавычках
    std::vector<Entry> entries;   // словарь — в порядке появления в файле
    std::vector<Node> items;      // список

    bool isScalar() const noexcept { return kind == Kind::Scalar; }
    bool isMapping() const noexcept { return kind == Kind::Mapping; }
    bool isSequence() const noexcept { return kind == Kind::Sequence; }

    // Пустое значение («ключ:» без продолжения) — null.
    bool isNull() const noexcept { return kind == Kind::Scalar && !quoted && scalar.empty(); }

    const Node* find(std::string_view key) const;
};

struct Entry {
    std::string key;
    int line = 0;
    Node value;
};

class ParseError : public std::runtime_error {
public:
    ParseError(int line, const std::string& detail);

    int line() const noexcept { return line_; }
    const std::string& detail() const noexcept { return detail_; }

private:
    int line_;
    std::string detail_;
};

// Разбирает документ; пустой документ — пустой словарь. Бросает ParseError.
Node parse(std::string_view text);

std::string_view kindName(Node::Kind kind);

}  // namespace mafia::yaml
