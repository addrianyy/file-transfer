cmake_minimum_required(VERSION 3.5)

add_library(zstd src/zstd.c src/zstd.h)
target_include_directories(zstd PUBLIC src)