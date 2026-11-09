#include "json.h"
#include "alloc_counter.hpp"
#include <iostream>
#include <string>

std::string makeJsonObject(int fieldCount) {
    std::string text = "{";

    for (int i = 0; i < fieldCount; ++i) {
        if (i > 0) {
            text += ",";
        }

        text += "\"long_field_name_" + std::to_string(i)
              + "\":\"long_value_data_" + std::to_string(i) + "\"";
    }

    text += "}";
    return text;
}

std::string makeJsonArray(int elementCount) {
    std::string text = "[";

    for (int i = 0; i < elementCount; ++i) {
        if (i > 0) {
            text += ",";
        }

        text += "\"long_item_value_" + std::to_string(i) + "\"";
    }

    text += "]";
    return text;
}

int main() {
    const std::string input = R"({
        "name": "张三",
        "age": 22,
        "university": "清华大学",
        "scores": [95, 88, 92],
        "graduated": false
    })";

    std::cout << "===== JSON parse demo =====\n";
    std::cout << "input JSON:\n" << input << '\n';

    json::jobject object = json::jobject::parse(input);

    const std::string name = object["name"];
    const int age = object["age"];
    const std::string university = object["university"];

    std::cout << "name: " << name << '\n';
    std::cout << "before: " << object.as_string() << '\n';
    std::cout << "age: " << age << '\n';
    std::cout << "university: " << university << '\n';


    object["age"] = 23;
    object["graduated"].set_boolean(true);
    std::cout << "local age: " << age << '\n';
    std::cout << "object age: " << static_cast<int>(object["age"]) << '\n';

    std::cout << "\nupdated JSON:\n" << object.pretty() << '\n';

    json::jobject created;
    created["project"] = "simpleson memory lab";
    created["version"] = 1;
    created["ready"].set_boolean(true);

       std::cout << "\ncreated JSON: " << created.as_string() << '\n';

    const std::string smallInput = makeJsonArray(100);

    AllocCounter::reset();

    {
         json::jobject measured = json::jobject::parse(smallInput);
    }
    AllocCounter::report("parse + destroy");

    const std::string accessInput =
        R"({"name_of_student":"Tom_the_great_one","age_of_student":25})";
    json::jobject accessObject = json::jobject::parse(accessInput);

    AllocCounter::reset();

    {
        std::string name = accessObject["name_of_student"];
    }

    AllocCounter::report("read string + destroy");

    int measuredAge = 0;
    AllocCounter::reset();

    {
        measuredAge = accessObject["age_of_student"];
    }

    AllocCounter::report("read int");
    std::cout << "read age: " << measuredAge << '\n';

    const std::string serializationInput = makeJsonObject(50);
    json::jobject serializationObject = json::jobject::parse(serializationInput);

    std::size_t compactSize = 0;
    AllocCounter::reset();

    {
        const std::string output = serializationObject.as_string();
        compactSize = output.size();
    }

    AllocCounter::report("as_string: 50 fields + destroy");

    std::size_t prettySize = 0;
    AllocCounter::reset();

    {
        const std::string output = serializationObject.pretty();
        prettySize = output.size();
    }

    AllocCounter::report("pretty: 50 fields + destroy");
    std::cout << "output bytes: compact=" << compactSize
              << ", pretty=" << prettySize << '\n';

    // Verify both outputs outside the allocation measurement intervals.
    if (serializationObject.as_string() != serializationInput ||
        json::jobject::parse(serializationObject.pretty()).as_string() !=
            serializationInput) {
        std::cerr << "Serialization round-trip failed\n";
        return 1;
    }

    std::cout << "serialization round-trip: PASS\n";

    return 0;
}
