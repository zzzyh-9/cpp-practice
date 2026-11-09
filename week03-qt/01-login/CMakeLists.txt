add_executable(week03_login WIN32 main.cpp)
target_link_libraries(week03_login PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

if(MSVC)
    target_compile_options(week03_login PRIVATE /utf-8)
endif()
