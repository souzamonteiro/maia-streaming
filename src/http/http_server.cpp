#include "maia/http/http_server.hpp"
#include "maia/core/utils.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstring>

namespace maia::http {

std::string HttpRequest::get_header(std::string_view name, std::string_view default_val) const {
    auto it = headers.find(std::string(name));
    if (it != headers.end()) {
        return it->second;
    }
    return std::string(default_val);
}

std::string HttpRequest::get_param(std::string_view name, std::string_view default_val) const {
    auto it = params.find(std::string(name));
    if (it != params.end()) {
        return it->second;
    }
    auto itq = query.find(std::string(name));
    if (itq != query.end()) {
        return itq->second;
    }
    return std::string(default_val);
}

void HttpResponse::set_header(std::string name, std::string value) {
    headers[std::move(name)] = std::move(value);
}

HttpResponse HttpResponse::text(int status, const std::string& text_body, const std::string& content_type) {
    HttpResponse resp;
    resp.status_code = status;
    resp.status_message = HttpServer::status_to_message(status);
    resp.body = text_body;
    resp.set_header("Content-Type", content_type);
    resp.set_header("Content-Length", std::to_string(resp.body.size()));
    return resp;
}

HttpResponse HttpResponse::json(int status, const core::Json& json_body) {
    HttpResponse resp;
    resp.status_code = status;
    resp.status_message = HttpServer::status_to_message(status);
    resp.body = json_body.dump();
    resp.set_header("Content-Type", "application/json; charset=utf-8");
    resp.set_header("Content-Length", std::to_string(resp.body.size()));
    return resp;
}

HttpResponse HttpResponse::file(int status, const std::string& file_path, std::uint64_t offset, std::uint64_t length,
                                 const std::string& content_type, const std::string& content_range, const std::string& etag) {
    HttpResponse resp;
    resp.status_code = status;
    resp.status_message = HttpServer::status_to_message(status);
    resp.is_file_stream = true;
    resp.file_path = file_path;
    resp.file_offset = offset;
    resp.file_length = length;
    resp.set_header("Content-Type", content_type);
    resp.set_header("Content-Length", std::to_string(length));
    resp.set_header("Accept-Ranges", "bytes");
    if (!content_range.empty()) {
        resp.set_header("Content-Range", content_range);
    }
    if (!etag.empty()) {
        resp.set_header("ETag", etag);
    }
    return resp;
}

HttpResponse HttpResponse::error(int status, const std::string& code, const std::string& message) {
    core::Json err = core::Json::object();
    err["status"] = static_cast<double>(status);
    err["error"] = code;
    err["message"] = message;
    return json(status, err);
}

std::string HttpServer::status_to_message(int code) {
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 206: return "Partial Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 416: return "Range Not Satisfiable";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        default:  return (code >= 200 && code < 300) ? "OK" : "Error";
    }
}

std::string HttpServer::url_decode(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            int high = in[i + 1];
            int low = in[i + 2];
            auto hex_val = [](int c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int h = hex_val(high);
            int l = hex_val(low);
            if (h >= 0 && l >= 0) {
                out.push_back(static_cast<char>((h << 4) | l));
                i += 2;
                continue;
            }
        } else if (in[i] == '+') {
            out.push_back(' ');
            continue;
        }
        out.push_back(in[i]);
    }
    return out;
}

void HttpServer::parse_query_string(std::string_view qs, std::map<std::string, std::string>& out) {
    while (!qs.empty()) {
        auto amp_pos = qs.find('&');
        auto pair = (amp_pos == std::string_view::npos) ? qs : qs.substr(0, amp_pos);
        if (amp_pos == std::string_view::npos) {
            qs = "";
        } else {
            qs = qs.substr(amp_pos + 1);
        }

        if (pair.empty()) continue;
        auto eq_pos = pair.find('=');
        if (eq_pos == std::string_view::npos) {
            out[url_decode(pair)] = "";
        } else {
            auto key = pair.substr(0, eq_pos);
            auto val = pair.substr(eq_pos + 1);
            out[url_decode(key)] = url_decode(val);
        }
    }
}

HttpServer::HttpServer(std::string host, uint16_t port, std::size_t worker_threads, std::size_t max_connections)
    : host_(std::move(host)), port_(port),
      worker_threads_count_(worker_threads > 0 ? worker_threads : 4),
      max_connections_(max_connections),
      thread_pool_(worker_threads_count_) {}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::get(const std::string& path, Handler handler) {
    add_route("GET", path, std::move(handler));
}

void HttpServer::post(const std::string& path, Handler handler) {
    add_route("POST", path, std::move(handler));
}

void HttpServer::put(const std::string& path, Handler handler) {
    add_route("PUT", path, std::move(handler));
}

void HttpServer::del(const std::string& path, Handler handler) {
    add_route("DELETE", path, std::move(handler));
}

void HttpServer::head(const std::string& path, Handler handler) {
    add_route("HEAD", path, std::move(handler));
}

void HttpServer::add_route(const std::string& method, const std::string& pattern, Handler handler) {
    std::lock_guard<std::mutex> lock(routes_mutex_);
    Route route;
    route.method = method;
    route.pattern = pattern;
    route.handler = std::move(handler);

    // Convert pattern "/v1/assets/{assetId}/jobs" to regex "^/v1/assets/([^/?#]+)/jobs$"
    std::string reg_str = "^";
    std::size_t i = 0;
    while (i < pattern.size()) {
        if (pattern[i] == '{') {
            auto end_brace = pattern.find('}', i);
            if (end_brace != std::string::npos) {
                std::string param_name = pattern.substr(i + 1, end_brace - i - 1);
                route.param_names.push_back(param_name);
                reg_str += "([^/?#]+)";
                i = end_brace + 1;
                continue;
            }
        }
        if (pattern[i] == '.' || pattern[i] == '[' || pattern[i] == ']' ||
            pattern[i] == '(' || pattern[i] == ')' || pattern[i] == '+' ||
            pattern[i] == '*' || pattern[i] == '?' || pattern[i] == '^' ||
            pattern[i] == '$' || pattern[i] == '|') {
            reg_str += '\\';
        }
        reg_str += pattern[i];
        ++i;
    }
    reg_str += "$";
    route.regex_pattern = std::regex(reg_str);
    routes_.push_back(std::move(route));
}

bool HttpServer::start() {
    if (running_.load()) return true;

    server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        std::cerr << "[HttpServer] Failed to create socket: " << strerror(errno) << std::endl;
        return false;
    }

    int opt = 1;
    ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
    ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    if (host_ == "0.0.0.0" || host_.empty()) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0) {
            std::cerr << "[HttpServer] Invalid host IP address: " << host_ << std::endl;
            ::close(server_fd_);
            server_fd_ = -1;
            return false;
        }
    }

    if (::bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[HttpServer] Bind error on " << host_ << ":" << port_ << ": " << strerror(errno) << std::endl;
        ::close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    if (::listen(server_fd_, static_cast<int>(max_connections_)) < 0) {
        std::cerr << "[HttpServer] Listen error: " << strerror(errno) << std::endl;
        ::close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    running_.store(true);
    accept_thread_ = std::thread([this]() { accept_loop(); });
    return true;
}

void HttpServer::stop() {
    if (!running_.exchange(false)) return;

    if (server_fd_ >= 0) {
        ::shutdown(server_fd_, SHUT_RDWR);
        ::close(server_fd_);
        server_fd_ = -1;
    }

    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    thread_pool_.stop();
}

void HttpServer::accept_loop() {
    while (running_.load()) {
        pollfd pfd{.fd = server_fd_, .events = POLLIN, .revents = 0};
        int ret = ::poll(&pfd, 1, 200);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) continue; // timeout

        if (pfd.revents & POLLIN) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            int client_fd = ::accept(server_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (client_fd >= 0) {
                thread_pool_.enqueue([this, client_fd]() {
                    handle_client(client_fd);
                });
            }
        }
    }
}

HttpResponse HttpServer::route_request(HttpRequest& req) {
    if (req.method == "OPTIONS") {
        HttpResponse resp;
        resp.status_code = 204;
        resp.status_message = "No Content";
        resp.set_header("Access-Control-Allow-Origin", "*");
        resp.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, HEAD, OPTIONS");
        resp.set_header("Access-Control-Allow-Headers", "*");
        resp.set_header("Access-Control-Max-Age", "86400");
        return resp;
    }

    std::lock_guard<std::mutex> lock(routes_mutex_);
    for (const auto& route : routes_) {
        bool method_matches = (route.method == req.method);
        if (!method_matches && req.method == "HEAD" && route.method == "GET") {
            method_matches = true;
        }

        if (method_matches) {
            std::smatch match;
            if (std::regex_match(req.path, match, route.regex_pattern)) {
                for (std::size_t i = 0; i < route.param_names.size() && (i + 1) < match.size(); ++i) {
                    req.params[route.param_names[i]] = url_decode(match[i + 1].str());
                }
                return route.handler(req);
            }
        }
    }

    return HttpResponse::error(404, "not_found", "Route not found: " + req.method + " " + req.path);
}

void HttpServer::handle_client(int client_fd) {
    timeval tv{.tv_sec = 10, .tv_usec = 0};
    ::setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    std::string buffer;
    buffer.reserve(8192);
    char chunk[4096];

    while (running_.load()) {
        auto header_end_pos = buffer.find("\r\n\r\n");
        while (header_end_pos == std::string::npos) {
            ssize_t n = ::recv(client_fd, chunk, sizeof(chunk), 0);
            if (n <= 0) {
                ::close(client_fd);
                return;
            }
            buffer.append(chunk, n);
            header_end_pos = buffer.find("\r\n\r\n");
        }

        std::string raw_headers = buffer.substr(0, header_end_pos);
        std::istringstream stream(raw_headers);
        std::string req_line;
        if (!std::getline(stream, req_line)) {
            ::close(client_fd);
            return;
        }
        if (!req_line.empty() && req_line.back() == '\r') {
            req_line.pop_back();
        }

        HttpRequest req;
        std::istringstream line_stream(req_line);
        std::string full_path, http_version;
        line_stream >> req.method >> full_path >> http_version;

        auto qmark = full_path.find('?');
        if (qmark != std::string::npos) {
            req.path = url_decode(full_path.substr(0, qmark));
            req.query_string = full_path.substr(qmark + 1);
            parse_query_string(req.query_string, req.query);
        } else {
            req.path = url_decode(full_path);
        }

        std::string hline;
        while (std::getline(stream, hline)) {
            if (!hline.empty() && hline.back() == '\r') hline.pop_back();
            if (hline.empty()) continue;
            auto colon = hline.find(':');
            if (colon != std::string::npos) {
                std::string k = core::trim(hline.substr(0, colon));
                std::string v = core::trim(hline.substr(colon + 1));
                req.headers[k] = v;
            }
        }

        std::size_t content_length = 0;
        std::string cl_hdr = req.get_header("Content-Length");
        if (!cl_hdr.empty()) {
            try {
                content_length = std::stoull(cl_hdr);
            } catch (...) {
                content_length = 0;
            }
        }

        std::size_t header_len = header_end_pos + 4;
        std::string remaining = buffer.substr(header_len);
        while (remaining.size() < content_length) {
            ssize_t n = ::recv(client_fd, chunk, sizeof(chunk), 0);
            if (n <= 0) break;
            remaining.append(chunk, n);
        }

        std::size_t body_len = std::min(content_length, remaining.size());
        req.body = remaining.substr(0, body_len);
        buffer = (remaining.size() > body_len) ? remaining.substr(body_len) : "";

        HttpResponse resp = route_request(req);

        std::string conn_hdr = req.get_header("Connection");
        bool close_conn = (conn_hdr == "close" || conn_hdr == "Close");

        resp.set_header("Server", "Maia-Streaming/0.1.0");
        resp.set_header("Access-Control-Allow-Origin", "*");
        resp.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, HEAD, OPTIONS");
        resp.set_header("Access-Control-Allow-Headers", "*");
        if (close_conn) {
            resp.set_header("Connection", "close");
        } else {
            resp.set_header("Connection", "keep-alive");
        }

        std::ostringstream response_stream;
        response_stream << "HTTP/1.1 " << resp.status_code << " " << resp.status_message << "\r\n";
        for (const auto& [k, v] : resp.headers) {
            response_stream << k << ": " << v << "\r\n";
        }
        response_stream << "\r\n";
        std::string head_bytes = response_stream.str();

        ssize_t sent = ::send(client_fd, head_bytes.data(), head_bytes.size(), 0);
        if (sent <= 0) {
            ::close(client_fd);
            return;
        }

        if (req.method != "HEAD") {
            if (resp.is_file_stream) {
                std::ifstream file(resp.file_path, std::ios::binary);
                if (file.is_open()) {
                    file.seekg(static_cast<std::streamoff>(resp.file_offset));
                    std::uint64_t bytes_to_send = resp.file_length;
                    char file_chunk[65536];
                    while (bytes_to_send > 0 && file.good()) {
                        std::size_t to_read = static_cast<std::size_t>(std::min<std::uint64_t>(bytes_to_send, sizeof(file_chunk)));
                        file.read(file_chunk, to_read);
                        std::streamsize read_bytes = file.gcount();
                        if (read_bytes <= 0) break;

                        ssize_t s = ::send(client_fd, file_chunk, static_cast<size_t>(read_bytes), 0);
                        if (s <= 0) break;
                        bytes_to_send -= static_cast<std::uint64_t>(s);
                    }
                }
            } else if (!resp.body.empty()) {
                const char* p = resp.body.data();
                std::size_t rem = resp.body.size();
                while (rem > 0) {
                    ssize_t s = ::send(client_fd, p, rem, 0);
                    if (s <= 0) break;
                    p += s;
                    rem -= static_cast<std::size_t>(s);
                }
            }
        }

        if (close_conn) {
            ::close(client_fd);
            return;
        }
    }

    ::close(client_fd);
}

} // namespace maia::http
