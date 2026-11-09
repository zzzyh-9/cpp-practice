#include "alloc_counter.hpp"
#include "json.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

std::string makeJsonObject(int fieldCount) {
    std::string json = "{";
    for (int i = 0; i < fieldCount; ++i) {
        if (i > 0) {
            json += ",";
        }
        json += "\"long_field_name_" + std::to_string(i) +
                "\":\"long_value_data_" + std::to_string(i) + "\"";
    }
    json += "}";
    return json;
}

std::string makeJsonArray(int elementCount) {
    std::string json = "[";
    for (int i = 0; i < elementCount; ++i) {
        if (i > 0) {
            json += ",";
        }
        json += "\"long_item_value_" + std::to_string(i) + "\"";
    }
    json += "]";
    return json;
}

bool require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "CHECK FAILED: " << message << '\n';
    }
    return condition;
}

bool verifyArrayApi() {
    bool ok = true;

    json::jobject array = json::jobject::parse(R"(["apple","banana","cherry"])");
    ok = require(array.is_array(), "root array should report is_array()") && ok;
    ok = require(array.size() == 3, "root array size should be 3") && ok;
    const std::string first = array.array(0);
    const std::string second = array.array(1);
    ok = require(first == "apple", "array(0) should be apple") && ok;
    ok = require(second == "banana", "array(1) should be banana") && ok;

    json::jobject object;
    object["fruits"] = std::vector<std::string>{"apple", "banana"};
    object["nums"] = std::vector<int>{1, 2, 3};
    const std::vector<std::string> fruits = object["fruits"];
    const std::vector<int> nums = object["nums"];
    ok = require(fruits == std::vector<std::string>({"apple", "banana"}),
                 "nested string array round-trip failed") && ok;
    ok = require(nums == std::vector<int>({1, 2, 3}),
                 "nested number array round-trip failed") && ok;

    json::jobject person1;
    json::jobject person2;
    person1["name"] = "Tom";
    person2["name"] = "Jerry";
    object["people"] = std::vector<json::jobject>{person1, person2};
    const std::vector<json::jobject> people = object["people"];
    const std::string name0 = people[0]["name"];
    ok = require(name0 == "Tom", "object array round-trip failed") && ok;

    std::cout << "[API] array, nested array and object array checks: "
              << (ok ? "PASS" : "FAIL") << "\n\n";
    return ok;
}

void measureParse(const char* label, const std::string& input,
                  bool useStringOverload = false) {
    std::cout << "--- " << label << " ---\n";
    AllocCounter::reset();
    {
        json::jobject object = useStringOverload
            ? json::jobject::parse(input)
            : json::jobject::parse(input.c_str());
        (void)object;
    }
    AllocCounter::report(useStringOverload
        ? "parse(string) + destroy" : "parse(c_str) + destroy");
}

void measureValueAccess() {
    const std::string input =
        R"({"name_of_student":"Tom_the_great_one","age_of_student":25,"city_of_origin":"Beijing_China_Asia"})";
    json::jobject object = json::jobject::parse(input);

    std::cout << "--- value access ---\n";
    AllocCounter::reset();
    {
        const std::string name = object["name_of_student"];
        (void)name;
    }
    AllocCounter::report("read string value");

    AllocCounter::reset();
    {
        const int age = object["age_of_student"];
        (void)age;
    }
    AllocCounter::report("read int value");
}

bool measureSerialization() {
    const std::string input = makeJsonObject(50);
    json::jobject object = json::jobject::parse(input);

    std::cout << "--- serialization ---\n";
    AllocCounter::reset();
    {
        const std::string output = object.as_string();
        (void)output;
    }
    AllocCounter::report("as_string()");

    AllocCounter::reset();
    {
        const std::string output = object.pretty();
        (void)output;
    }
    AllocCounter::report("pretty()");

    // Validate outside the measurement intervals so checks do not affect counts.
    const bool compactOk = require(object.as_string() == input,
                                   "compact serialization changed the contents");
    const bool prettyOk = require(
        json::jobject::parse(object.pretty()).as_string() == input,
        "pretty serialization round-trip failed");
    std::cout << "[API] serialization round-trip checks: "
              << (compactOk && prettyOk ? "PASS" : "FAIL") << "\n\n";
    return compactOk && prettyOk;
}

} // namespace

int main() {
    std::cout << "========================================\n";
    std::cout << "  Simpleson allocation analysis: " << LAB_VARIANT << '\n';
    std::cout << "========================================\n\n";

    if (!verifyArrayApi()) {
        return 1;
    }

    std::cout << "=== Part 2: worksheet measurements (c_str entry) ===\n\n";
    measureParse("small JSON: 2 fields",
                 R"({"name_of_student":"Tom_the_great_one","age_of_student":25})");
    measureParse("medium JSON: 10 fields", makeJsonObject(10));
    measureParse("large JSON: 100 fields", makeJsonObject(100));
    measureParse("JSON array: 100 elements", makeJsonArray(100));
    measureValueAccess();
    if (!measureSerialization()) {
        return 1;
    }

    std::cout << "=== Extra: string-parameter comparison ===\n\n";
    measureParse("small JSON: 2 fields",
                 R"({"name_of_student":"Tom_the_great_one","age_of_student":25})",
                 true);
    measureParse("medium JSON: 10 fields", makeJsonObject(10), true);
    measureParse("large JSON: 100 fields", makeJsonObject(100), true);
    measureParse("JSON array: 100 elements", makeJsonArray(100), true);

    std::cout << "Allocation analysis complete.\n";
    return 0;
}
