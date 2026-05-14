cmake_minimum_required(VERSION 4.0)
project(Lab2)

set(CMAKE_CXX_STANDARD 20)

add_executable(Lab2 main.cpp sender.cpp receiver.cpp)
target_link_libraries(Lab2 ws2_32)
