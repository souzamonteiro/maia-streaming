#include "maia/core/service.hpp"
#include <iostream>
#include <csignal>
#include <string>
#include <vector>
#include <filesystem>
#include <thread>
#include <chrono>

namespace {
std::atomic_bool g_shutdown_requested{false};

void signal_handler(int sig) {
    std::cout << "\n[Maia-Streaming] Caught signal " << sig << ", initiating graceful shutdown...\n";
    g_shutdown_requested.store(true);
}
}

int main(int argc, char* argv[]) {
    std::string config_path = "config/maia-streaming.toml";
    std::string bind_arg;
    int port_arg = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            std::cout << "Maia Streaming Server v0.1.0\n"
                      << "Usage: " << argv[0] << " [options]\n\n"
                      << "Options:\n"
                      << "  -c, --config <file>   Path to configuration TOML file\n"
                      << "  -b, --bind <address>  Bind IP address (default: 127.0.0.1)\n"
                      << "  -p, --port <port>     Bind TCP port (default: 8090)\n"
                      << "  -v, --version         Show version information\n"
                      << "  -h, --help            Show this help message\n";
            return 0;
        } else if (arg == "-v" || arg == "--version") {
            std::cout << "Maia Streaming Server v0.1.0 (C++20 Native Media Plane)\n";
            return 0;
        } else if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            config_path = argv[++i];
        } else if ((arg == "-b" || arg == "--bind") && i + 1 < argc) {
            bind_arg = argv[++i];
        } else if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            port_arg = std::atoi(argv[++i]);
        }
    }

    // Fall back to example config if specified config does not exist
    if (!std::filesystem::exists(config_path) && std::filesystem::exists("config/maia-streaming.example.toml")) {
        config_path = "config/maia-streaming.example.toml";
    }

    std::cout << "========================================================\n"
              << "            Maia Streaming Server v0.1.0\n"
              << "========================================================\n";

    maia::core::Config cfg;
    if (std::filesystem::exists(config_path)) {
        std::cout << "[Config] Loading configuration from " << config_path << '\n';
        cfg = maia::core::Config::load_from_file(config_path);
    } else {
        std::cout << "[Config] Using default configuration\n";
        cfg = maia::core::Config::default_config();
    }

    if (!bind_arg.empty()) {
        cfg.server.bind = bind_arg;
    }
    if (port_arg > 0) {
        cfg.server.port = static_cast<uint16_t>(port_arg);
    }

    // Set signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    maia::core::Service service(cfg);
    if (!service.start()) {
        std::cerr << "[Maia-Streaming] Fatal error: Could not start service\n";
        return 1;
    }

    std::cout << "[Maia-Streaming] Service ready at http://"
              << cfg.server.bind << ":" << cfg.server.port << "/\n";
    std::cout << "[Maia-Streaming] HTML5 Player available at http://"
              << cfg.server.bind << ":" << cfg.server.port << "/player\n";
    std::cout << "[Maia-Streaming] Press Ctrl+C to terminate\n";

    while (!g_shutdown_requested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    service.stop();
    std::cout << "[Maia-Streaming] Shutdown complete.\n";
    return 0;
}
