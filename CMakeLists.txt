cmake_minimum_required(VERSION 3.20)
project(maia_streaming VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

option(MAIA_ENABLE_WEBRTC "Build the Maia WebRTC/SFU module" ON)
option(MAIA_BUILD_TESTS "Build tests" ON)

# Include directory
include_directories(include)

# 1. maia_core
add_library(maia_core
    src/core/utils.cpp
    src/core/config.cpp
    src/core/thread_pool.cpp
)
target_link_libraries(maia_core PUBLIC OpenSSL::SSL OpenSSL::Crypto Threads::Threads)

# 2. maia_auth
add_library(maia_auth
    src/auth/token.cpp
)
target_link_libraries(maia_auth PUBLIC maia_core)

# 3. maia_storage
add_library(maia_storage
    src/storage/storage.cpp
)
target_link_libraries(maia_storage PUBLIC maia_core)

# 4. maia_vod
add_library(maia_vod
    src/vod/range.cpp
)
target_link_libraries(maia_vod PUBLIC maia_core)

# 5. maia_media
add_library(maia_media
    src/media/asset.cpp
    src/media/job.cpp
    src/media/recording.cpp
)
target_link_libraries(maia_media PUBLIC maia_core maia_storage)

# 6. maia_hls
add_library(maia_hls
    src/hls/hls.cpp
)
target_link_libraries(maia_hls PUBLIC maia_core maia_storage maia_media)

# 7. maia_webrtc
add_library(maia_webrtc
    src/webrtc/sfu.cpp
)
target_link_libraries(maia_webrtc PUBLIC maia_core maia_media)

# 8. maia_metrics
add_library(maia_metrics
    src/metrics/metrics.cpp
)
target_link_libraries(maia_metrics PUBLIC maia_core)

# 9. maia_http
add_library(maia_http
    src/http/http_server.cpp
)
target_link_libraries(maia_http PUBLIC maia_core)

# 10. maia_service
add_library(maia_service
    src/core/service.cpp
)
target_link_libraries(maia_service PUBLIC
    maia_core
    maia_auth
    maia_storage
    maia_vod
    maia_media
    maia_hls
    maia_webrtc
    maia_metrics
    maia_http
)

# Executable
add_executable(maia-streaming src/main.cpp)
target_link_libraries(maia-streaming PRIVATE maia_service)

# Tests
if(MAIA_BUILD_TESTS)
    enable_testing()

    add_executable(range_test tests/range_test.cpp)
    target_link_libraries(range_test PRIVATE maia_vod)
    add_test(NAME range_test COMMAND range_test)

    add_executable(token_test tests/token_test.cpp)
    target_link_libraries(token_test PRIVATE maia_auth)
    add_test(NAME token_test COMMAND token_test)

    add_executable(storage_test tests/storage_test.cpp)
    target_link_libraries(storage_test PRIVATE maia_storage)
    add_test(NAME storage_test COMMAND storage_test)

    add_executable(asset_test tests/asset_test.cpp)
    target_link_libraries(asset_test PRIVATE maia_media)
    add_test(NAME asset_test COMMAND asset_test)

    add_executable(sfu_test tests/sfu_test.cpp)
    target_link_libraries(sfu_test PRIVATE maia_webrtc)
    add_test(NAME sfu_test COMMAND sfu_test)

    add_executable(integration_test tests/integration_test.cpp)
    target_link_libraries(integration_test PRIVATE maia_service)
    add_test(NAME integration_test COMMAND integration_test)
endif()
