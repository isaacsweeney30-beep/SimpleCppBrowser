cmake_minimum_required(VERSION 3.16)
project(SimpleCppBrowser LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenSSL REQUIRED)

# Add executable
add_executable(browser main.cpp)
target_link_libraries(browser PRIVATE OpenSSL::SSL OpenSSL::Crypto pthread)

# Copy UI files to build directory
add_custom_command(TARGET browser POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/ui ${CMAKE_BINARY_DIR}/ui
    COMMENT "Copying UI files to build directory"
)

# Set compile definitions for UI path
target_compile_definitions(browser PRIVATE 
    UI_PATH="${CMAKE_BINARY_DIR}/ui"
    DEFAULT_LANGUAGE="en"
)
