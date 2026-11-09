cmake_minimum_required(VERSION 3.16)
project(week03_practice LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)

find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets)
set(QT_VERSION_MAJOR 5)

add_subdirectory(01-login)
add_subdirectory(02-hello-labels)
add_subdirectory(04-rescue-apple)
