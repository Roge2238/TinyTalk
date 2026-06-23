cmake_minimum_required(VERSION 3.10)
project(Tinytalk VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(Tinytalk
    Mainserver.cpp
    server.cpp
    inbox.cpp
)

find_package(Threads REQUIRED)
target_link_libraries(Tinytalk Threads::Threads)