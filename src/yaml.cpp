#include "mafia/yaml.hpp"

#include <optional>
#include <utility>

namespace mafia::yaml {

namespace {

struct Line {
    int number;
    int indent;
    std::string text;  // без отступа, комментария и хвостовых пробелов
};

struct KeyValue {
    std::string key;
    std::string rest;
};

bool isBlank(char c) {
    return c == ' ' || c == '\t';
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && isBlank(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && isBlank(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

// Кавычка открывает строку только в начале значения, а не внутри слова (it's).
bool canOpenQuote(std::string_view text, std::size_t i) {
    return i == 0 || text[i - 1] == ' ' || text[i - 1] == '[' || text[i - 1] == ',';
}

// Отрезает комментарий, не трогая «#» внутри кавычек и внутри слов (C#).
std::string_view stripComment(std::string_view text) {
    char quote = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quote != 0) {
            if (quote == '"' && c == '\\') {
                ++i;
            } else if (c == quote) {
                quote = 0;
            }
        } else if ((c == '"' || c == '\'') && canOpenQuote(text, i)) {
            quote = c;
        } else if (c == '#' && (i == 0 || isBlank(text[i - 1]))) {
            return text.substr(0, i);
        }
    }
    return text;
}

std::vector<Line> splitLines(std::string_view text) {
    std::vector<Line> lines;
    int number = 0;
    std::size_t start = 0;
    for (;;) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view raw = text.substr(start, end - start);
        ++number;
        if (!raw.empty() && raw.back() == '\r') {
            raw.remove_suffix(1);
        }
        if (number == 1 && raw.starts_with("\xEF\xBB\xBF")) {
            raw.remove_prefix(3);  // BOM
        }

        std::size_t indent = 0;
        while (indent < raw.size() && isBlank(raw[indent])) {
            ++indent;
        }
        const std::string_view content = trim(stripComment(raw.substr(indent)));
        if (!content.empty()) {
            if (raw.substr(0, indent).find('\t') != std::string_view::npos) {
                throw ParseError(number, "табуляция в отступе — в YAML отступы делаются только пробелами");
            }
            if (indent == 0 && content == "---") {
                if (!lines.empty()) {
                    throw ParseError(number, "несколько документов в одном файле не поддерживаются");
                }
            } else if (indent == 0 && content == "...") {
                break;
            } else {
                lines.push_back({number, static_cast<int>(indent), std::string(content)});
            }
        }
        if (end >= text.size()) {
            break;
        }
        start = end + 1;
    }
    return lines;
}

// Строка в кавычках, начиная с text[0]; end — позиция за закрывающей кавычкой.
std::string parseQuoted(std::string_view text, int line, std::size_t& end) {
    const char quote = text.front();
    std::string out;
    for (std::size_t i = 1; i < text.size(); ++i) {
        const char c = text[i];
        if (quote == '\'') {
            if (c == '\'') {
                if (i + 1 < text.size() && text[i + 1] == '\'') {
                    out += '\'';
                    ++i;
                    continue;
                }
                end = i + 1;
                return out;
            }
            out += c;
            continue;
        }
        if (c == '\\') {
            if (i + 1 >= text.size()) {
                break;
            }
            const char escaped = text[++i];
            switch (escaped) {
                case 'n':
                    out += '\n';
                    break;
                case 't':
                    out += '\t';
                    break;
                case '"':
                case '\\':
                case '/':
                    out += escaped;
                    break;
                default:
                    throw ParseError(line, std::string("неизвестная escape-последовательность «\\") + escaped + "»");
            }
            continue;
        }
        if (c == '"') {
            end = i + 1;
            return out;
        }
        out += c;
    }
    throw ParseError(line, "незакрытая кавычка");
}

bool isSequenceItem(std::string_view text) {
    return text == "-" || text.starts_with("- ");
}

// «ключ: значение» → {ключ, значение}; для прочих строк — nullopt.
std::optional<KeyValue> splitKey(std::string_view text, int line) {
    if (text.empty() || text.front() == '[' || text.front() == '{') {
        return std::nullopt;
    }
    if (text.front() == '"' || text.front() == '\'') {
        std::size_t end = 0;
        std::string key = parseQuoted(text, line, end);
        std::size_t i = end;
        while (i < text.size() && text[i] == ' ') {
            ++i;
        }
        if (i < text.size() && text[i] == ':' && (i + 1 == text.size() || text[i + 1] == ' ')) {
            return KeyValue{std::move(key), std::string(trim(text.substr(i + 1)))};
        }
        return std::nullopt;
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == ':' && (i + 1 == text.size() || text[i + 1] == ' ')) {
            const std::string_view key = trim(text.substr(0, i));
            if (key.empty()) {
                throw ParseError(line, "пустой ключ перед «:»");
            }
            return KeyValue{std::string(key), std::string(trim(text.substr(i + 1)))};
        }
    }
    return std::nullopt;
}

Node parseValue(std::string_view text, int line);

Node parseFlowSequence(std::string_view text, int line) {
    Node node;
    node.kind = Node::Kind::Sequence;
    node.line = line;
    if (text.back() != ']') {
        throw ParseError(line, "список […] должен закрываться «]» в той же строке");
    }
    const std::string_view inner = trim(text.substr(1, text.size() - 2));
    if (inner.empty()) {
        return node;
    }
    std::size_t start = 0;
    char quote = 0;
    for (std::size_t i = 0; i <= inner.size(); ++i) {
        if (i < inner.size()) {
            const char c = inner[i];
            if (quote != 0) {
                if (quote == '"' && c == '\\') {
                    ++i;
                } else if (c == quote) {
                    quote = 0;
                }
                continue;
            }
            if ((c == '"' || c == '\'') && trim(inner.substr(start, i - start)).empty()) {
                quote = c;
                continue;
            }
            if (c == '[' || c == ']' || c == '{' || c == '}') {
                throw ParseError(line, "вложенные скобки внутри […] не поддерживаются");
            }
            if (c != ',') {
                continue;
            }
        }
        const std::string_view item = trim(inner.substr(start, i - start));
        if (item.empty()) {
            throw ParseError(line, "пустой элемент в списке […]");
        }
        node.items.push_back(parseValue(item, line));
        start = i + 1;
    }
    return node;
}

Node parseValue(std::string_view text, int line) {
    Node node;
    node.line = line;
    const char first = text.front();
    if (first == '"' || first == '\'') {
        std::size_t end = 0;
        node.scalar = parseQuoted(text, line, end);
        node.quoted = true;
        if (!trim(text.substr(end)).empty()) {
            throw ParseError(line, "лишние символы после закрывающей кавычки");
        }
        return node;
    }
    if (first == '[') {
        return parseFlowSequence(text, line);
    }
    if (first == '{') {
        throw ParseError(line, "словари в фигурных скобках {…} не поддерживаются — используйте блочную запись");
    }
    if (first == '&' || first == '*') {
        throw ParseError(line, "якоря и ссылки (&, *) не поддерживаются");
    }
    if (first == '!') {
        throw ParseError(line, "теги (!) не поддерживаются");
    }
    if (first == '|' || first == '>') {
        throw ParseError(line, "многострочные блоки (|, >) не поддерживаются");
    }
    if (first == '@' || first == '`' || first == '%') {
        throw ParseError(line, std::string("значение не может начинаться с «") + first + "»");
    }
    if (text.find(": ") != std::string_view::npos || text.back() == ':') {
        throw ParseError(line, "в значении встретилось «:» — возьмите значение в кавычки");
    }
    node.scalar = std::string(text);
    return node;
}

class Parser {
public:
    explicit Parser(std::vector<Line> lines) : lines_(std::move(lines)) {}

    Node parseDocument() {
        if (lines_.empty()) {
            Node empty;
            empty.kind = Node::Kind::Mapping;
            empty.line = 1;
            return empty;
        }
        Node root = parseBlock(lines_.front().indent);
        if (!atEnd()) {
            throw ParseError(current().number, "строка не вписывается в структуру документа — проверьте отступы");
        }
        return root;
    }

private:
    bool atEnd() const { return pos_ >= lines_.size(); }
    Line& current() { return lines_[pos_]; }

    Node parseBlock(int indent) {
        const Line& line = current();
        if (isSequenceItem(line.text)) {
            return parseSequence(indent);
        }
        if (splitKey(line.text, line.number)) {
            return parseMapping(indent);
        }
        ++pos_;
        Node node = parseValue(line.text, line.number);
        if (!atEnd() && current().indent > indent) {
            throw ParseError(current().number, "неожиданный отступ после значения");
        }
        return node;
    }

    Node parseMapping(int indent) {
        Node node;
        node.kind = Node::Kind::Mapping;
        node.line = current().number;
        while (!atEnd()) {
            const Line line = current();
            if (line.indent < indent) {
                break;
            }
            if (line.indent > indent) {
                throw ParseError(line.number, "неожиданный отступ");
            }
            if (isSequenceItem(line.text)) {
                throw ParseError(line.number, "элемент списка «- …» внутри словаря — ожидалось «ключ: значение»");
            }
            auto pair = splitKey(line.text, line.number);
            if (!pair) {
                throw ParseError(line.number, "ожидалось «ключ: значение», а встретилось «" + line.text + "»");
            }
            for (const Entry& existing : node.entries) {
                if (existing.key == pair->key) {
                    throw ParseError(line.number, "повторяющийся ключ «" + pair->key + "» (впервые — в строке " +
                                                      std::to_string(existing.line) + ")");
                }
            }
            ++pos_;

            Entry entry{pair->key, line.number, Node{}};
            if (pair->rest.empty()) {
                if (!atEnd() && current().indent > indent) {
                    entry.value = parseBlock(current().indent);
                } else if (!atEnd() && current().indent == indent && isSequenceItem(current().text)) {
                    entry.value = parseSequence(indent);  // «ключ:» и список на том же отступе
                } else {
                    entry.value.line = line.number;  // null
                }
            } else {
                entry.value = parseValue(pair->rest, line.number);
                if (!atEnd() && current().indent > indent) {
                    throw ParseError(current().number, "неожиданный отступ: у ключа «" + pair->key +
                                                           "» уже есть значение в строке " +
                                                           std::to_string(line.number));
                }
            }
            node.entries.push_back(std::move(entry));
        }
        return node;
    }

    Node parseSequence(int indent) {
        Node node;
        node.kind = Node::Kind::Sequence;
        node.line = current().number;
        while (!atEnd()) {
            Line& line = current();
            if (line.indent < indent) {
                break;
            }
            if (line.indent > indent) {
                throw ParseError(line.number, "неожиданный отступ");
            }
            if (!isSequenceItem(line.text)) {
                break;
            }
            std::size_t offset = 1;
            while (offset < line.text.size() && line.text[offset] == ' ') {
                ++offset;
            }
            const std::string rest = line.text.substr(offset);
            const int number = line.number;
            if (rest.empty()) {
                ++pos_;
                if (!atEnd() && current().indent > indent) {
                    node.items.push_back(parseBlock(current().indent));
                } else {
                    Node null;
                    null.line = number;
                    node.items.push_back(std::move(null));
                }
            } else if (isSequenceItem(rest) || splitKey(rest, number)) {
                // «- ключ: значение»: элемент — блок с отступом своего первого символа.
                line.indent = indent + static_cast<int>(offset);
                line.text = rest;
                node.items.push_back(parseBlock(line.indent));
            } else {
                ++pos_;
                node.items.push_back(parseValue(rest, number));
                if (!atEnd() && current().indent > indent) {
                    throw ParseError(current().number, "неожиданный отступ после элемента списка");
                }
            }
        }
        return node;
    }

    std::vector<Line> lines_;
    std::size_t pos_ = 0;
};

}  // namespace

const Node* Node::find(std::string_view key) const {
    for (const Entry& entry : entries) {
        if (entry.key == key) {
            return &entry.value;
        }
    }
    return nullptr;
}

ParseError::ParseError(int line, const std::string& detail)
    : std::runtime_error("строка " + std::to_string(line) + ": " + detail), line_(line), detail_(detail) {}

Node parse(std::string_view text) {
    return Parser(splitLines(text)).parseDocument();
}

std::string_view kindName(Node::Kind kind) {
    switch (kind) {
        case Node::Kind::Scalar:
            return "значение";
        case Node::Kind::Mapping:
            return "словарь";
        case Node::Kind::Sequence:
            return "список";
    }
    return "?";
}

}  // namespace mafia::yaml
