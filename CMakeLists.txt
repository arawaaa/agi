cmake_minimum_required(VERSION 3.0)

project(sched_converter)

add_executable(sched_converter main.cpp)

install(TARGETS sched_converter RUNTIME DESTINATION bin)
