add_executable(week03_hello_labels WIN32 main.cpp)
target_link_libraries(week03_hello_labels PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

if(MSVC)
    target_compile_options(week03_hello_labels PRIVATE /utf-8)
endif()
