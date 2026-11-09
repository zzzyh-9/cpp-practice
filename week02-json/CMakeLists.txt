cmake_minimum_required(VERSION 3.16)

project(simpleson_memory_lab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(simpleson_baseline STATIC baseline/simpleson/json.cpp)
target_include_directories(simpleson_baseline PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/baseline/simpleson"
)

add_library(simpleson_optimized STATIC simpleson/json.cpp)
target_include_directories(simpleson_optimized PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/simpleson"
)

add_library(alloc_counter STATIC alloc_counter.cpp)
target_include_directories(alloc_counter PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_executable(json_demo src/json_demo.cpp)
target_link_libraries(json_demo PRIVATE simpleson_optimized alloc_counter)

if(MINGW)
    target_link_options(json_demo PRIVATE -static-libstdc++ -static-libgcc)
endif()

add_executable(json_demo_baseline src/json_demo.cpp)
target_link_libraries(json_demo_baseline PRIVATE simpleson_baseline alloc_counter)

if(MINGW)
    target_link_options(json_demo_baseline PRIVATE -static-libstdc++ -static-libgcc)
endif()

add_executable(memory_test_baseline src/memory_test.cpp)
target_link_libraries(memory_test_baseline PRIVATE simpleson_baseline alloc_counter)
target_compile_definitions(memory_test_baseline PRIVATE LAB_VARIANT="baseline")

add_executable(memory_test_optimized src/memory_test.cpp)
target_link_libraries(memory_test_optimized PRIVATE simpleson_optimized alloc_counter)
target_compile_definitions(memory_test_optimized PRIVATE LAB_VARIANT="optimized")

if(MINGW)
    target_link_options(memory_test_baseline PRIVATE -static-libstdc++ -static-libgcc)
    target_link_options(memory_test_optimized PRIVATE -static-libstdc++ -static-libgcc)
endif()

if (MSVC)
    target_compile_options(alloc_counter PRIVATE /W4 /utf-8)
    target_compile_options(json_demo PRIVATE /W4 /utf-8)
    target_compile_options(json_demo_baseline PRIVATE /W4 /utf-8)
    target_compile_options(memory_test_baseline PRIVATE /W4 /utf-8)
    target_compile_options(memory_test_optimized PRIVATE /W4 /utf-8)
else()
    target_compile_options(alloc_counter PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(json_demo PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(json_demo_baseline PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(memory_test_baseline PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(memory_test_optimized PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_test(NAME json_demo_baseline COMMAND json_demo_baseline)
add_test(NAME json_demo_optimized COMMAND json_demo)
add_test(NAME memory_test_baseline COMMAND memory_test_baseline)
add_test(NAME memory_test_optimized COMMAND memory_test_optimized)

# Exercise the changed serializer against both the original and optimized code.
foreach(variant IN ITEMS baseline optimized)
    add_executable(serialization_test_${variant} src/serialization_test.cpp)
    target_link_libraries(serialization_test_${variant} PRIVATE simpleson_${variant})
    if(MINGW)
        target_link_options(serialization_test_${variant} PRIVATE
            -static-libstdc++ -static-libgcc)
    endif()
    if(MSVC)
        target_compile_options(serialization_test_${variant} PRIVATE /W4 /utf-8)
    else()
        target_compile_options(serialization_test_${variant} PRIVATE
            -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME serialization_test_${variant} COMMAND serialization_test_${variant})
endforeach()
