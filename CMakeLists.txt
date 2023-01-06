cmake_minimum_required(VERSION 3.24)
project(server)

set(CMAKE_CXX_STANDARD 23)

add_executable(server main.cpp server.h thread_pool.h)
