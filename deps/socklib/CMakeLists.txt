cmake_minimum_required(VERSION 3.20)

add_library(socklib STATIC "")
add_subdirectory(src)

target_include_directories(socklib PUBLIC src)
target_compile_features(socklib PUBLIC cxx_std_20)

if (WIN32)
    target_link_libraries(socklib PRIVATE "ws2_32.lib")
endif()