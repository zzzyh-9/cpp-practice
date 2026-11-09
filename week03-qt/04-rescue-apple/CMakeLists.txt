set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Multimedia)

add_library(apple_game_core STATIC model/fallinggamemodel.cpp model/fallinggamemodel.h)
target_include_directories(apple_game_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(apple_game_core PUBLIC Qt${QT_VERSION_MAJOR}::Core)

add_library(apple_game_ui STATIC
    view/skinmanager.cpp view/skinmanager.h
    view/gameview.cpp view/gameview.h
    view/gamewindow.cpp view/gamewindow.h
    controller/soundplayer.cpp controller/soundplayer.h
    controller/gamecontroller.cpp controller/gamecontroller.h
    apple_resources.qrc
)
target_link_libraries(apple_game_ui PUBLIC apple_game_core
    Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Multimedia)

add_executable(week03_rescue_apple WIN32 main.cpp)
target_link_libraries(week03_rescue_apple PRIVATE apple_game_ui)

if(MSVC)
    target_compile_options(apple_game_core PUBLIC /utf-8)
endif()

if(BUILD_TESTING)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)
    add_executable(apple_model_tests tests/model_test.cpp)
    target_link_libraries(apple_model_tests PRIVATE apple_game_core Qt${QT_VERSION_MAJOR}::Test)
    add_test(NAME apple_model_tests COMMAND apple_model_tests)

    add_executable(apple_view_tests tests/view_test.cpp)
    target_link_libraries(apple_view_tests PRIVATE apple_game_ui Qt${QT_VERSION_MAJOR}::Test)
    add_test(NAME apple_view_tests COMMAND apple_view_tests)
    set_tests_properties(apple_view_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
