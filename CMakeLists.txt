cmake_minimum_required(VERSION 3.16)

project(problem_015_file_storage LANGUAGES CXX)

add_executable(code main.cpp)

target_compile_features(code PRIVATE cxx_std_17)
target_compile_options(code PRIVATE -O2 -Wall -Wextra -Wpedantic)
