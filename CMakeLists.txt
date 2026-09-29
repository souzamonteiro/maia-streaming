cmake_minimum_required(VERSION 3.20)
project(maia_streaming VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MAIA_ENABLE_WEBRTC "Build the extracted Maia WebRTC/SFU module" OFF)
option(MAIA_BUILD_TESTS "Build tests" ON)

add_library(maia_core
    src/core/service.cpp
)
target_include_directories(maia_core PUBLIC include)

add_library(maia_vod
    src/vod/range.cpp
)
target_include_directories(maia_vod PUBLIC include)
target_link_libraries(maia_vod PUBLIC maia_core)

add_executable(maia-streaming src/main.cpp)
target_link_libraries(maia-streaming PRIVATE maia_core maia_vod)

if(MAIA_BUILD_TESTS)
    enable_testing()
    add_executable(range_test tests/range_test.cpp)
    target_link_libraries(range_test PRIVATE maia_vod)
    add_test(NAME range_test COMMAND range_test)
endif()
