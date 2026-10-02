#include "maia/core/config.hpp"
#include "maia/core/utils.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdlib>

namespace maia::core {

Config Config::default_config() {
    Config cfg;
    // Check environment variables if present
    if (const char* env_bind = std::getenv("MAIA_SERVER_BIND")) {
        cfg.server.bind = env_bind;
    }
    if (const char* env_port = std::getenv("MAIA_SERVER_PORT")) {
        cfg.server.port = static_cast<uint16_t>(std::atoi(env_port));
    }
    if (const char* env_root = std::getenv("MAIA_STORAGE_ROOT")) {
        cfg.storage.root = env_root;
    }
    if (const char* env_secret = std::getenv("MAIA_AUTH_SECRET")) {
        cfg.auth.secret_key = env_secret;
    }
    return cfg;
}

Config Config::load_from_file(const std::string& path) {
    Config cfg = default_config();
    std::ifstream file(path);
    if (!file.is_open()) {
        return cfg;
    }

    std::string current_section;
    std::string line;

    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line.starts_with('#') || line.starts_with(';')) {
            continue;
        }

        if (line.starts_with('[') && line.ends_with(']')) {
            current_section = to_lower(trim(line.substr(1, line.size() - 2)));
            continue;
        }

        auto eq_pos = line.find('=');
        if (eq_pos == std::string::npos) continue;

        std::string key = to_lower(trim(line.substr(0, eq_pos)));
        std::string val = trim(line.substr(eq_pos + 1));

        // Strip quotes if any
        if (val.size() >= 2 && ((val.front() == '"' && val.back() == '"') || (val.front() == '\'' && val.back() == '\''))) {
            val = val.substr(1, val.size() - 2);
        }

        auto parse_bool = [](const std::string& v) {
            return (v == "true" || v == "1" || v == "yes" || v == "on");
        };

        if (current_section == "server") {
            if (key == "bind") cfg.server.bind = val;
            else if (key == "port") cfg.server.port = static_cast<uint16_t>(std::stoul(val));
            else if (key == "worker_threads") cfg.server.worker_threads = std::stoul(val);
            else if (key == "max_connections") cfg.server.max_connections = std::stoul(val);
        } else if (current_section == "storage") {
            if (key == "type") cfg.storage.type = val;
            else if (key == "root") cfg.storage.root = val;
        } else if (current_section == "vod") {
            if (key == "max_range_bytes") cfg.vod.max_range_bytes = std::stoull(val);
            else if (key == "send_chunk_bytes") cfg.vod.send_chunk_bytes = std::stoul(val);
        } else if (current_section == "auth") {
            if (key == "issuer") cfg.auth.issuer = val;
            else if (key == "audience") cfg.auth.audience = val;
            else if (key == "secret_key") cfg.auth.secret_key = val;
            else if (key == "require_tokens") cfg.auth.require_tokens = parse_bool(val);
        } else if (current_section == "processing") {
            if (key == "max_parallel_jobs") cfg.processing.max_parallel_jobs = std::stoul(val);
            else if (key == "ffmpeg") cfg.processing.ffmpeg = val;
            else if (key == "ffprobe") cfg.processing.ffprobe = val;
        } else if (current_section == "metrics") {
            if (key == "enabled") cfg.metrics.enabled = parse_bool(val);
            else if (key == "path") cfg.metrics.path = val;
        } else if (current_section == "webrtc") {
            if (key == "enabled") cfg.webrtc.enabled = parse_bool(val);
            else if (key == "udp_port_start") cfg.webrtc.udp_port_start = static_cast<uint16_t>(std::stoul(val));
            else if (key == "udp_port_end") cfg.webrtc.udp_port_end = static_cast<uint16_t>(std::stoul(val));
        }
    }

    return cfg;
}

bool Config::save_to_file(const std::string& path) const {
    std::ofstream file(path);
    if (!file.is_open()) return false;

    file << "[server]\n";
    file << "bind = \"" << server.bind << "\"\n";
    file << "port = " << server.port << "\n";
    file << "worker_threads = " << server.worker_threads << "\n";
    file << "max_connections = " << server.max_connections << "\n\n";

    file << "[storage]\n";
    file << "type = \"" << storage.type << "\"\n";
    file << "root = \"" << storage.root << "\"\n\n";

    file << "[vod]\n";
    file << "max_range_bytes = " << vod.max_range_bytes << "\n";
    file << "send_chunk_bytes = " << vod.send_chunk_bytes << "\n\n";

    file << "[auth]\n";
    file << "issuer = \"" << auth.issuer << "\"\n";
    file << "audience = \"" << auth.audience << "\"\n";
    file << "require_tokens = " << (auth.require_tokens ? "true" : "false") << "\n\n";

    file << "[processing]\n";
    file << "max_parallel_jobs = " << processing.max_parallel_jobs << "\n";
    file << "ffmpeg = \"" << processing.ffmpeg << "\"\n";
    file << "ffprobe = \"" << processing.ffprobe << "\"\n\n";

    file << "[metrics]\n";
    file << "enabled = " << (metrics.enabled ? "true" : "false") << "\n";
    file << "path = \"" << metrics.path << "\"\n\n";

    file << "[webrtc]\n";
    file << "enabled = " << (webrtc.enabled ? "true" : "false") << "\n";
    file << "udp_port_start = " << webrtc.udp_port_start << "\n";
    file << "udp_port_end = " << webrtc.udp_port_end << "\n";

    return true;
}

} // namespace maia::core

