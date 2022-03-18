cmake_minimum_required(VERSION 3.5.1)

# set the project name
project(Sender-Receiver VERSION 1.0)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)


SET(CMAKE_BUILD_TYPE "Debug")
SET(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -Wall -g2 -ggdb")
SET(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O3 -Wall")


add_executable(send-main send-main.cpp util.cpp send-module.cpp)
add_executable(recv-main recv-main.cpp util.cpp recv-module.cpp)