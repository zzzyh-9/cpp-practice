#include "json.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

bool check(const std::string& name, const std::string& input,
           const std::string& expected) {
    try {
    const json::jobject object = json::jobject::parse(input);
    const std::string actual = object.as_string();
    if (actual != expected) {
        std::cerr << "FAIL: " << name << "\nexpected: " << expected
                  << "\nactual:   " << actual << '\n';
        return false;
    }
    if (json::jobject::parse(actual).as_string() != expected) {
        std::cerr << "FAIL: round-trip / " << name << '\n';
        return false;
    }
    std::cout << "PASS: " << name << '\n';
    return true;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << name << " / " << error.what() << '\n';
        return false;
    }
}

std::string objectInput(int fields, int keyLength) {
    std::string result = "{";
    for (int i = 0; i < fields; ++i) {
        if (i) result += ',';
        result += '"' + std::string(keyLength, 'k') + std::to_string(i) +
                  "\":\"" + std::string(40, 'v') + std::to_string(i) + '"';
    }
    result += '}';
    return result;
}

std::string arrayInput(int count) {
    std::string result = "[";
    for (int i = 0; i < count; ++i) {
        if (i) result += ',';
        result += '"' + std::string(50, 'x') + std::to_string(i) + '"';
    }
    result += ']';
    return result;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--probe-escaped-keys") {
        try {
            const auto object = json::jobject::parse(
                R"({"quote\"key":"quote\"value","back\\key":"back\\value","slash\/key":"slash\/value"})");
            std::cout << "Parser probe: ACCEPTED\n" << object.as_string() << '\n';
        } catch (const std::exception& error) {
            std::cout << "Parser probe: REJECTED / " << error.what() << '\n';
        }
        return 0; // Informational probe; this is not a passing round-trip test.
    }
    // Exact expected output checks complement round-trips: a serializer and
    // parser agreeing with each other alone would not prove byte compatibility.
    const std::vector<std::string> fixtures = {
        "{}", "[]", "{\"one\":1}", "[1,true,false,null,\"text\"]",
        R"({"nested":{"a":[1,2]},"empty":{},"list":[]})",
        R"([{"a":1},[2,3],"a,b:c",null])",
        R"({"tab\tkey":1,"line\nkey":2,"return\rkey":3,"back\bkey":4,"form\fkey":5})",
        u8R"({"姓名":"张三","学校":"清华大学","emoji":"😀"})"
    };
    bool ok = true;
    int count = 0;
    for (const auto& input : fixtures) {
        ok = check("fixture " + std::to_string(++count), input, input) && ok;
    }
    // The upstream parser rejects this particular escaped-key document. Build
    // it through the API and verify the serializer against exact JSON bytes.
    json::jobject escapedKeys;
    escapedKeys["quote\"key"] = "quote\"value";
    escapedKeys["back\\key"] = "back\\value";
    escapedKeys["slash/key"] = "slash/value";
    const std::string escapedExpected =
        R"({"quote\"key":"quote\"value","back\\key":"back\\value","slash\/key":"slash\/value"})";
    if (escapedKeys.as_string() != escapedExpected) {
        std::cerr << "FAIL: programmatic escaped keys\n";
        ok = false;
    } else {
        std::cout << "PASS: programmatic escaped keys (exact output only)\n";
    }
    ++count;
    for (int fields : {0, 1, 10, 50, 100}) {
        const std::string input = objectInput(fields, 18);
        ok = check("object fields=" + std::to_string(fields), input, input) && ok;
        ++count;
    }
    for (int length : {0, 7, 14, 15, 16, 17, 30, 100, 4096}) {
        const std::string input = objectInput(3, length);
        ok = check("key length=" + std::to_string(length), input, input) && ok;
        ++count;
    }
    for (int length : {0, 1, 100, 1000}) {
        const std::string input = arrayInput(length);
        ok = check("array size=" + std::to_string(length), input, input) && ok;
        ++count;
    }
    // Many escaped key characters make the reservation estimate smaller than
    // the actual output. Appending must still grow correctly in that case.
    std::string escaped = "{";
    for (int i = 0; i < 20; ++i) {
        if (i) escaped += ',';
        escaped += '"';
        for (int j = 0; j < 40; ++j) escaped += R"(\t)";
        escaped += std::to_string(i) + "\":true";
    }
    escaped += '}';
    ok = check("escape-heavy keys", escaped, escaped) && ok;
    ++count;
    ok = check("whitespace normalization", " { \"a\" : 1 , \"b\" : 2 } ",
               "{\"a\":1,\"b\":2}") && ok;
    ++count;
    std::cout << count << " serialization cases: " << (ok ? "PASS" : "FAIL") << '\n';
    return ok ? 0 : 1;
}
