cmake_minimum_required(VERSION 3.16)
project(SimpleCppBrowser LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenSSL REQUIRED)

add_executable(browser main.cpp)
target_link_libraries(browser PRIVATE OpenSSL::SSL OpenSSL::Crypto pthread)
