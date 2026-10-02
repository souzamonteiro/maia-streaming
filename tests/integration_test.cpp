#include "maia/core/service.hpp"
#include "maia/core/utils.hpp"
#include "maia/core/json.hpp"
#include <cassert>
#include <iostream>
#include <filesystem>
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sstream>

struct HttpResult {
    int status = 0;
    std::string raw_headers;
    std::string body;
    std::map<std::string, std::string> headers;

    std::string get_header(std::string_view name) const {
        for (const auto& [k, v] : headers) {
            if (maia::core::iequals(k, name)) {
                return v;
            }
        }
        return "";
    }
};

static HttpResult http_request(const std::string& method, const std::string& path,
                               const std::vector<std::string>& extra_headers = {},
                               const std::string& body = "", int port = 18080) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    int conn = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (conn < 0) {
        ::close(fd);
        return HttpResult{};
    }

    std::ostringstream req;
    req << method << " " << path << " HTTP/1.1\r\n";
    req << "Host: 127.0.0.1:" << port << "\r\n";
    req << "Connection: close\r\n";

    bool has_cl = false;
    for (const auto& h : extra_headers) {
        req << h << "\r\n";
        if (h.rfind("Content-Length:", 0) == 0 || h.rfind("content-length:", 0) == 0) {
            has_cl = true;
        }
    }
    if (!has_cl && !body.empty()) {
        req << "Content-Length: " << body.size() << "\r\n";
    }
    req << "\r\n";
    if (!body.empty()) {
        req << body;
    }

    std::string wire = req.str();
    ssize_t sent = ::send(fd, wire.data(), wire.size(), 0);
    assert(sent == static_cast<ssize_t>(wire.size()));

    std::string resp_data;
    char buf[4096];
    while (true) {
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        resp_data.append(buf, n);
    }
    ::close(fd);

    HttpResult res;
    auto pos = resp_data.find("\r\n\r\n");
    if (pos == std::string::npos) return res;

    res.raw_headers = resp_data.substr(0, pos);
    res.body = resp_data.substr(pos + 4);

    std::istringstream hstream(res.raw_headers);
    std::string line;
    if (std::getline(hstream, line)) {
        std::istringstream lstream(line);
        std::string proto;
        lstream >> proto >> res.status;
    }

    while (std::getline(hstream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto colon = line.find(':');
        if (colon != std::string::npos) {
            std::string k = maia::core::trim(line.substr(0, colon));
            std::string v = maia::core::trim(line.substr(colon + 1));
            res.headers[k] = v;
        }
    }

    return res;
}

int main() {
    std::filesystem::path test_dir = "/tmp/maia_integration_test_storage";
    std::filesystem::remove_all(test_dir);

    maia::core::Config config;
    config.server.bind = "127.0.0.1";
    config.server.port = 18080;
    config.server.worker_threads = 4;
    config.storage.root = test_dir.string();
    config.auth.require_tokens = true;
    config.auth.secret_key = "integration-secret-test-key";

    auto service = std::make_unique<maia::core::Service>(config);
    assert(service->start());
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    std::cout << "[Integration] Starting 15 E2E tests against live service...\n";

    // 1. GET /healthz
    auto r1 = http_request("GET", "/healthz");
    assert(r1.status == 200);
    assert(r1.body.find("\"status\":\"ok\"") != std::string::npos);
    std::cout << "  [1/15] GET /healthz OK\n";

    // 2. GET /readyz
    auto r2 = http_request("GET", "/readyz");
    assert(r2.status == 200);
    assert(r2.body.find("\"status\":\"ready\"") != std::string::npos);
    std::cout << "  [2/15] GET /readyz OK\n";

    // 3. GET /metrics
    auto r3 = http_request("GET", "/metrics");
    assert(r3.status == 200);
    assert(r3.body.find("maia_http_requests_total") != std::string::npos);
    std::cout << "  [3/15] GET /metrics (Prometheus) OK\n";

    // 4. POST /v1/assets (Create asset with base64 data)
    std::string fake_media = std::string("RIFF\x24\x00\x00\x00WAVEfmt \x10\x00\x00\x00", 20) + "1234567890abcdefghijklmnopqrstuvwxyz";
    std::string b64 = maia::core::base64_encode(fake_media);
    maia::core::Json asset_payload = maia::core::Json::object();
    asset_payload["name"] = "test_audio.wav";
    asset_payload["data_base64"] = b64;
    asset_payload["mime_type"] = "audio/wav";

    std::string post_asset_body = asset_payload.dump();
    auto r4 = http_request("POST", "/v1/assets", {"Content-Type: application/json"}, post_asset_body);
    assert(r4.status == 201);
    auto created_asset = maia::core::Json::parse(r4.body);
    assert(created_asset.has_value());
    std::string asset_id = created_asset->get("id").as_string();
    assert(!asset_id.empty());
    std::cout << "  [4/15] POST /v1/assets created ID: " << asset_id << " OK\n";

    // 5. GET /v1/assets/{assetId}
    auto r5 = http_request("GET", "/v1/assets/" + asset_id);
    assert(r5.status == 200);
    auto got_asset = maia::core::Json::parse(r5.body);
    assert(got_asset.has_value() && got_asset->get("title").as_string() == "test_audio.wav");
    std::cout << "  [5/15] GET /v1/assets/{id} OK\n";

    // 6. POST /v1/assets/{assetId}/playback-token
    maia::core::Json token_req = maia::core::Json::object();
    token_req["ttl_seconds"] = 3600.0;
    token_req["op"] = "play";
    auto r6 = http_request("POST", "/v1/assets/" + asset_id + "/playback-token",
                           {"Content-Type: application/json"}, token_req.dump());
    assert(r6.status == 200);
    auto token_resp = maia::core::Json::parse(r6.body);
    assert(token_resp.has_value() && token_resp->contains("token"));
    std::string token = token_resp->get("token").as_string();
    assert(!token.empty());
    std::cout << "  [6/15] POST /v1/assets/{id}/playback-token OK\n";

    // 7. GET /v1/media/{assetId}/content without token (Unauthorized check)
    auto r7 = http_request("GET", "/v1/media/" + asset_id + "/content");
    assert(r7.status == 401);
    std::cout << "  [7/15] GET /v1/media/{id}/content 401 (Auth enforcement) OK\n";

    // 8. GET /v1/media/{assetId}/content?token=... (200 OK full content)
    auto r8 = http_request("GET", "/v1/media/" + asset_id + "/content?token=" + token);
    assert(r8.status == 200);
    assert(r8.body == fake_media);
    std::string etag = r8.get_header("ETag");
    assert(!etag.empty());
    std::cout << "  [8/15] GET /v1/media/{id}/content 200 OK (Full content, ETag: " << etag << ")\n";

    // 9. GET /v1/media/{assetId}/content?token=... with Range: bytes=0-9 (206 Partial Content)
    auto r9 = http_request("GET", "/v1/media/" + asset_id + "/content?token=" + token,
                           {"Range: bytes=0-9"});
    assert(r9.status == 206);
    assert(r9.body == fake_media.substr(0, 10));
    std::string cr = r9.get_header("Content-Range");
    assert(cr.find("bytes 0-9/") != std::string::npos);
    std::cout << "  [9/15] GET /v1/media/{id}/content 206 Partial Content OK\n";

    // 10. GET /v1/media/{assetId}/content?token=... with If-None-Match: etag (304 Not Modified)
    auto r10 = http_request("GET", "/v1/media/" + asset_id + "/content?token=" + token,
                            {"If-None-Match: " + etag});
    assert(r10.status == 304);
    assert(r10.body.empty());
    std::cout << "  [10/15] GET /v1/media/{id}/content 304 Not Modified OK\n";

    // 11. GET /v1/media/{assetId}/content?token=... with Range: bytes=99999- (416 Range Not Satisfiable)
    auto r11 = http_request("GET", "/v1/media/" + asset_id + "/content?token=" + token,
                            {"Range: bytes=99999-"});
    assert(r11.status == 416);
    std::cout << "  [11/15] GET /v1/media/{id}/content 416 Unsatisfiable OK\n";

    // 12. GET /v1/hls/{assetId}/master.m3u8?token=... (HLS Master Playlist)
    auto r12 = http_request("GET", "/v1/hls/" + asset_id + "/master.m3u8?token=" + token);
    assert(r12.status == 200);
    assert(r12.body.find("#EXTM3U") != std::string::npos);
    assert(r12.body.find("#EXT-X-STREAM-INF") != std::string::npos);
    std::cout << "  [12/15] GET /v1/hls/{id}/master.m3u8 OK\n";

    // 13. POST /v1/assets/{assetId}/jobs (Media Processing Pipeline)
    maia::core::Json job_req = maia::core::Json::object();
    job_req["type"] = "thumbnail";
    auto r13 = http_request("POST", "/v1/assets/" + asset_id + "/jobs",
                            {"Content-Type: application/json"}, job_req.dump());
    assert(r13.status == 201);
    auto job_resp = maia::core::Json::parse(r13.body);
    assert(job_resp.has_value() && job_resp->contains("id"));
    std::cout << "  [13/15] POST /v1/assets/{id}/jobs OK\n";

    // 14. POST /v1/webrtc/rooms (WebRTC SFU Room Creation)
    maia::core::Json room_req = maia::core::Json::object();
    room_req["name"] = "Production-Stage";
    auto r14 = http_request("POST", "/v1/webrtc/rooms",
                            {"Content-Type: application/json"}, room_req.dump());
    assert(r14.status == 201);
    auto room_resp = maia::core::Json::parse(r14.body);
    assert(room_resp.has_value() && room_resp->contains("id"));
    std::cout << "  [14/15] POST /v1/webrtc/rooms OK\n";

    // 15. DELETE /v1/assets/{assetId}
    auto r15 = http_request("DELETE", "/v1/assets/" + asset_id);
    assert(r15.status == 200);
    auto r15_check = http_request("GET", "/v1/assets/" + asset_id);
    assert(r15_check.status == 404);
    std::cout << "  [15/15] DELETE /v1/assets/{id} OK\n";

    service->stop();
    std::filesystem::remove_all(test_dir);
    std::cout << "\n==========================================\n";
    std::cout << "  [SUCCESS] All 15/15 Integration Checks Passed!\n";
    std::cout << "==========================================\n";
    return 0;
}
