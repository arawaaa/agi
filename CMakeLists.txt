cmake_minimum_required(VERSION 3.15)

project(sched_converter)

set(CMAKE_CXX_STANDARD 20)

find_package(Boost 1.83 COMPONENTS json REQUIRED)
add_subdirectory(or-tools)

add_executable(sched_converter main.cpp machine.cpp job.cpp sorter.cpp)
target_link_libraries(sched_converter ortools::ortools)
target_link_libraries(sched_converter Boost::json)

install(TARGETS sched_converter RUNTIME DESTINATION bin)
