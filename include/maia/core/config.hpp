#pragma once

#include <string>
#include <cstdint>
#include <optional>

namespace maia::core {

struct ServerConfig {
    std::string bind = "127.0.0.1";
    uint16_t port = 8090;
    std::size_t worker_threads = 4;
    std::size_t max_connections = 4096;
};

struct StorageConfig {
    std::string type = "filesystem";
    std::string root = "/srv/maia/media";
};

struct VodConfig {
    std::uint64_t max_range_bytes = 67108864; // 64 MB
    std::size_t send_chunk_bytes = 1048576;    // 1 MB
};

struct AuthConfig {
    std::string issuer = "maia-platform";
    std::string audience = "maia-streaming";
    std::string secret_key = "maia-default-secret-key-for-dev";
    bool require_tokens = false; // if false, allows playing without token if token not supplied
};

struct ProcessingConfig {
    std::size_t max_parallel_jobs = 2;
    std::string ffmpeg = "/usr/bin/ffmpeg";
    std::string ffprobe = "/usr/bin/ffprobe";
};

struct MetricsConfig {
    bool enabled = true;
    std::string path = "/metrics";
};

struct WebRtcConfig {
    bool enabled = true;
    uint16_t udp_port_start = 20000;
    uint16_t udp_port_end = 20100;
};

struct Config {
    ServerConfig server;
    StorageConfig storage;
    VodConfig vod;
    AuthConfig auth;
    ProcessingConfig processing;
    MetricsConfig metrics;
    WebRtcConfig webrtc;

    static Config default_config();
    static Config load_from_file(const std::string& path);
    bool save_to_file(const std::string& path) const;
};

} // namespace maia::core

