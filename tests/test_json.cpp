// Тесты собственного JSON: разбор, сериализация, ошибки.

#include <string>

#include "mafia/json.hpp"
#include "test_framework.hpp"

using mafia::json::Array;
using mafia::json::Object;
using mafia::json::ParseError;
using mafia::json::Value;

TEST(ParsesAllKinds) {
    const Value value = mafia::json::parse(
        R"( {"null": null, "yes": true, "no": false, "int": -42, "real": 2.5e2,
             "text": "строка", "list": [1, [2, {}], []], "nested": {"a": {"b": "c"}}} )");
    CHECK(value.isObject());
    CHECK(value.find("null")->isNull());
    CHECK(value.find("yes")->asBool());
    CHECK(!value.find("no")->asBool());
    CHECK_EQ(value.find("int")->asNumber(), -42.0);
    CHECK_EQ(value.find("real")->asNumber(), 250.0);
    CHECK_EQ(value.find("text")->asString(), std::string("строка"));
    CHECK_EQ(value.find("list")->asArray().size(), std::size_t{3});
    CHECK_EQ(value.find("nested")->find("a")->find("b")->asString(), std::string("c"));
    CHECK(value.find("missing") == nullptr);
    CHECK(Value(5).find("x") == nullptr);
}

TEST(DecodesEscapesAndUnicode) {
    const Value value = mafia::json::parse(R"(["a\"b\\c\/d\n\t", "\u0416\u0443\u043a", "\ud83d\ude00"])");
    const Array& items = value.asArray();
    CHECK_EQ(items[0].asString(), std::string("a\"b\\c/d\n\t"));
    CHECK_EQ(items[1].asString(), std::string("Жук"));
    CHECK_EQ(items[2].asString(), std::string("\xF0\x9F\x98\x80"));  // эмодзи из суррогатной пары
}

TEST(DumpRoundTripsAndKeepsKeyOrder) {
    const Value value = Object{{"b", 1}, {"a", Array{true, nullptr, "x\"y\n"}}, {"c", 0.5}, {"d", "Ж"}};
    const std::string text = value.dump();
    CHECK_EQ(text, std::string(R"({"b":1,"a":[true,null,"x\"y\n"],"c":0.5,"d":"Ж"})"));
    CHECK(mafia::json::parse(text) == value);
    CHECK_EQ(Value(std::string("\x01")).dump(), std::string("\"\\u0001\""));
}

TEST(ReportsErrorsWithOffset) {
    const auto expectError = [](const std::string& text, const std::string& part) {
        try {
            mafia::json::parse(text);
            mafia_test::fail(__FILE__, __LINE__, "ожидалась ошибка для: " + text);
        } catch (const ParseError& error) {
            if (std::string(error.what()).find(part) == std::string::npos) {
                mafia_test::fail(__FILE__, __LINE__, std::string("«") + error.what() + "» не содержит «" + part + "»");
            }
        }
    };
    expectError("{\"a\": 1", "ожидалось «,» или «}»");
    expectError("{a: 1}", "ключ в кавычках");
    expectError("[1, 2,]", "неожиданный символ");
    expectError("\"open", "незакрытая строка");
    expectError("\"bad \\q\"", "escape");
    expectError("\"\\ud83d\"", "суррогат");
    expectError("01", "лишние символы");
    expectError("tru", "ожидалось «true»");
    expectError("{} {}", "лишние символы");
    expectError("", "неожиданный конец");
    expectError(std::string(100, '['), "вложенность");
}

MAFIA_TEST_MAIN()
