// Тесты YAML-парсера: поддерживаемое подмножество и сообщения об ошибках с номерами строк.

#include <string>

#include "mafia/yaml.hpp"
#include "test_framework.hpp"

using mafia::yaml::Node;
using mafia::yaml::ParseError;

namespace {

// Номер строки и текст ошибки разбора.
void expectError(const std::string& text, int line, const std::string& substring) {
    try {
        mafia::yaml::parse(text);
        mafia_test::fail(__FILE__, __LINE__, "ожидалась ошибка разбора для:\n" + text);
    } catch (const ParseError& error) {
        CHECK_EQ(error.line(), line);
        if (error.detail().find(substring) == std::string::npos) {
            mafia_test::fail(__FILE__, __LINE__, "«" + error.detail() + "» не содержит «" + substring + "»");
        }
        CHECK(std::string(error.what()).starts_with("строка " + std::to_string(line) + ": "));
    }
}

}  // namespace

TEST(NestedMappingKeepsOrderAndLines) {
    const Node root = mafia::yaml::parse(
        "---\n"
        "# комментарий\n"
        "roles:\n"
        "  mafia: auto   # хвостовой комментарий\n"
        "\n"
        "  doctor: 1\n"
        "title: Мафия\n");
    CHECK(root.isMapping());
    CHECK_EQ(root.entries.size(), std::size_t{2});
    const Node* roles = root.find("roles");
    CHECK(roles != nullptr && roles->isMapping());
    CHECK_EQ(roles->entries.size(), std::size_t{2});
    CHECK_EQ(roles->entries[0].key, std::string("mafia"));
    CHECK_EQ(roles->entries[0].value.scalar, std::string("auto"));
    CHECK_EQ(roles->entries[0].line, 4);
    CHECK_EQ(roles->entries[1].key, std::string("doctor"));
    CHECK_EQ(roles->entries[1].line, 6);
    CHECK_EQ(root.find("title")->scalar, std::string("Мафия"));
    CHECK(root.find("missing") == nullptr);
}

TEST(BlockSequences) {
    const Node root = mafia::yaml::parse(
        "plain:\n"
        "  - one\n"
        "  - two\n"
        "compact:\n"
        "- a\n"
        "- b\n"
        "objects:\n"
        "  - name: x\n"
        "    count: 2\n"
        "  - name: y\n"
        "nested:\n"
        "  - - 1\n"
        "    - 2\n");
    const Node* plain = root.find("plain");
    CHECK(plain->isSequence());
    CHECK_EQ(plain->items.size(), std::size_t{2});
    CHECK_EQ(plain->items[1].scalar, std::string("two"));

    const Node* compact = root.find("compact");
    CHECK(compact->isSequence());
    CHECK_EQ(compact->items.size(), std::size_t{2});

    const Node* objects = root.find("objects");
    CHECK_EQ(objects->items.size(), std::size_t{2});
    CHECK(objects->items[0].isMapping());
    CHECK_EQ(objects->items[0].find("count")->scalar, std::string("2"));
    CHECK_EQ(objects->items[1].find("name")->scalar, std::string("y"));

    const Node* nested = root.find("nested");
    CHECK_EQ(nested->items.size(), std::size_t{1});
    CHECK(nested->items[0].isSequence());
    CHECK_EQ(nested->items[0].items.size(), std::size_t{2});
}

TEST(FlowSequences) {
    const Node root = mafia::yaml::parse("list: [a, \"b, c\", 'd''e', 7]\nempty: []\n");
    const Node* list = root.find("list");
    CHECK(list->isSequence());
    CHECK_EQ(list->items.size(), std::size_t{4});
    CHECK_EQ(list->items[1].scalar, std::string("b, c"));
    CHECK_EQ(list->items[2].scalar, std::string("d'e"));
    CHECK(root.find("empty")->isSequence());
    CHECK(root.find("empty")->items.empty());
}

TEST(QuotedScalarsAndComments) {
    const Node root = mafia::yaml::parse(
        "a: \"x # не комментарий\"\n"
        "b: 'single # тоже'\n"
        "c: \"line\\nbreak \\\"q\\\"\"\n"
        "\"quoted key\": 1\n"
        "d: C# язык\n"
        "e: it's fine\n");
    CHECK_EQ(root.find("a")->scalar, std::string("x # не комментарий"));
    CHECK(root.find("a")->quoted);
    CHECK_EQ(root.find("b")->scalar, std::string("single # тоже"));
    CHECK_EQ(root.find("c")->scalar, std::string("line\nbreak \"q\""));
    CHECK_EQ(root.find("quoted key")->scalar, std::string("1"));
    CHECK_EQ(root.find("d")->scalar, std::string("C# язык"));
    CHECK_EQ(root.find("e")->scalar, std::string("it's fine"));
}

TEST(NullsEmptyDocumentAndLineEndings) {
    const Node root = mafia::yaml::parse("\xEF\xBB\xBFkey:\r\nother: 1\r\n");
    CHECK(root.find("key")->isNull());
    CHECK_EQ(root.find("key")->line, 1);
    CHECK_EQ(root.find("other")->scalar, std::string("1"));

    const Node empty = mafia::yaml::parse("# только комментарий\n\n");
    CHECK(empty.isMapping());
    CHECK(empty.entries.empty());

    const Node ended = mafia::yaml::parse("a: 1\n...\nэто: не читается\n");
    CHECK_EQ(ended.entries.size(), std::size_t{1});
}

TEST(ErrorsReportLineNumbers) {
    expectError("roles:\n  mafia: 1\n\tdoctor: 1\n", 3, "табуляция");
    expectError("a: 1\nb: 2\na: 3\n", 3, "повторяющийся ключ «a» (впервые — в строке 1)");
    expectError("roles:\n    mafia: 1\n  doctor: 1\n", 3, "отступ");
    expectError("a: 1\n  b: 2\n", 2, "уже есть значение в строке 1");
    expectError("a: \"open\n", 1, "незакрытая кавычка");
    expectError("a: &anchor 1\n", 1, "якоря");
    expectError("a: |\n  text\n", 1, "многострочные");
    expectError("a: {b: 1}\n", 1, "фигурных");
    expectError("a: !tag 1\n", 1, "теги");
    expectError("a: b: c\n", 1, "возьмите значение в кавычки");
    expectError("a:\n  b: 1\n  - c\n", 3, "элемент списка");
    expectError("a: 1\n---\nb: 2\n", 2, "несколько документов");
    expectError("a: [1, [2]]\n", 1, "вложенные");
    expectError("a: [1, 2\n", 1, "«]»");
    expectError("a: \"x\" y\n", 1, "лишние символы");
    expectError("a: \"bad \\q\"\n", 1, "escape");
    expectError("list:\n  - a\n  oops\n", 3, "неожиданный отступ");
    expectError("a: 1\njust text\n", 2, "ожидалось «ключ: значение»");
}

MAFIA_TEST_MAIN()
