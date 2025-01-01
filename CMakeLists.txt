cmake_minimum_required(VERSION 3.20)
project(file_transfer)

add_subdirectory(deps/baselib)
add_subdirectory(deps/socklib)
add_subdirectory(deps/zstd)

add_executable(ft "")
add_subdirectory(src)

target_link_libraries(ft PUBLIC baselib socklib zstd)
target_compile_features(ft PUBLIC cxx_std_20)
target_include_directories(ft PUBLIC src)
