cmake_minimum_required(VERSION 3.10)
project(obftest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find curl
find_package(CURL REQUIRED)

# Add executable
add_executable(obftest main.cpp)

# Link libraries
target_link_libraries(obftest ${CURL_LIBRARIES})
target_include_directories(obftest PRIVATE ${CURL_INCLUDE_DIRS})

# Compiler-specific options
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(obftest PRIVATE -Wall -Wextra -pthread)
endif()

