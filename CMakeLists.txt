cmake_minimum_required(VERSION 3.10)
project(ota_test)        # 这里定义项目名，警告就会消失

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(test_exe src/test.cpp)
