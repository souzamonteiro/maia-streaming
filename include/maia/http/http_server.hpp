#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <regex>
#include <atomic>
#include <mutex>
#include <thread>
#include <cstdint>
#include <cctype>
#include "maia/core/json.hpp"
#include "maia/core/thread_pool.hpp"

namespace maia::http {

struct CaseInsensitiveCompare {
    bool operator()(std::string_view lhs, std::string_view rhs) const noexcept {
        auto it1 = lhs.begin(), it2 = rhs.begin();
        while (it1 != lhs.end() && it2 != rhs.end()) {
            unsigned char c1 = static_cast<unsigned char>(std::tolower(*it1));
            unsigned char c2 = static_cast<unsigned char>(std::tolower(*it2));
            if (c1 < c2) return true;
            if (c1 > c2) return false;
            ++it1;
            ++it2;
        }
        return lhs.size() < rhs.size();
    }
};

struct HttpRequest {
    std::string method;
    std::string path;
    std::string query_string;
    std::map<std::string, std::string> query;
    std::map<std::string, std::string, CaseInsensitiveCompare> headers;
    std::map<std::string, std::string> params;
    std::string body;

    [[nodiscard]] std::string get_header(std::string_view name, std::string_view default_val = "") const;
    [[nodiscard]] std::string get_param(std::string_view name, std::string_view default_val = "") const;
};

struct HttpResponse {
    int status_code = 200;
    std::string status_message = "OK";
    std::map<std::string, std::string> headers;
    std::string body;

    bool is_file_stream = false;
    std::string file_path;
    std::uint64_t file_offset = 0;
    std::uint64_t file_length = 0;

    void set_header(std::string name, std::string value);

    static HttpResponse text(int status, const std::string& text_body, const std::string& content_type = "text/plain; charset=utf-8");
    static HttpResponse json(int status, const core::Json& json_body);
    static HttpResponse file(int status, const std::string& file_path, std::uint64_t offset, std::uint64_t length,
                             const std::string& content_type, const std::string& content_range = "", const std::string& etag = "");
    static HttpResponse error(int status, const std::string& code, const std::string& message);
};

using Handler = std::function<HttpResponse(const HttpRequest&)>;

struct Route {
    std::string method;
    std::string pattern;
    std::regex regex_pattern;
    std::vector<std::string> param_names;
    Handler handler;
};

class HttpServer {
public:
    HttpServer(std::string host, uint16_t port, std::size_t worker_threads = 4, std::size_t max_connections = 1024);
    ~HttpServer();

    void get(const std::string& path, Handler handler);
    void post(const std::string& path, Handler handler);
    void put(const std::string& path, Handler handler);
    void del(const std::string& path, Handler handler);
    void head(const std::string& path, Handler handler);
    void add_route(const std::string& method, const std::string& pattern, Handler handler);

    bool start();
    void stop();

    [[nodiscard]] bool running() const noexcept { return running_.load(); }
    [[nodiscard]] uint16_t port() const noexcept { return port_; }
    [[nodiscard]] const std::string& host() const noexcept { return host_; }

    static std::string status_to_message(int code);
    static std::string url_decode(std::string_view in);
    static void parse_query_string(std::string_view qs, std::map<std::string, std::string>& out);

private:
    void accept_loop();
    void handle_client(int client_fd);
    HttpResponse route_request(HttpRequest& req);

    std::string host_;
    uint16_t port_;
    std::size_t worker_threads_count_;
    std::size_t max_connections_;
    std::atomic<bool> running_{false};
    int server_fd_{-1};

    std::thread accept_thread_;
    core::ThreadPool thread_pool_;

    std::vector<Route> routes_;
    mutable std::mutex routes_mutex_;
};

} // namespace maia::http
