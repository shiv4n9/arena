cmake_minimum_required(VERSION 3.15)
project(ARCTIC VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /WX")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror -pedantic")
endif()

include_directories(src)

# Core library
add_library(arctic_core
    src/ou_sampler.cpp
    src/single_agent.cpp
    src/live_latency.cpp
)

if(WIN32)
    target_link_libraries(arctic_core PRIVATE ws2_32)
endif()

# Main executable
add_executable(arctic src/main.cpp)
target_link_libraries(arctic PRIVATE arctic_core)

# Tests
enable_testing()
add_executable(test_ou_sampler tests/test_ou_sampler.cpp)
target_link_libraries(test_ou_sampler PRIVATE arctic_core)
add_test(NAME test_ou_sampler COMMAND test_ou_sampler)

# Sweep Generator
add_executable(arctic_sweep src/sweep_generator.cpp)
target_link_libraries(arctic_sweep PRIVATE arctic_core)

add_executable(test_ks src/test_ks.cpp)
target_link_libraries(test_ks PRIVATE arctic_core)

add_executable(live_adaptation_logger src/live_adaptation_logger.cpp)
target_link_libraries(live_adaptation_logger PRIVATE arctic_core)

if(WIN32)
    target_link_libraries(arctic PRIVATE ws2_32)
    target_link_libraries(arctic_sweep PRIVATE ws2_32)
    target_link_libraries(test_ks PRIVATE ws2_32)
    target_link_libraries(live_adaptation_logger PRIVATE ws2_32)
endif()
