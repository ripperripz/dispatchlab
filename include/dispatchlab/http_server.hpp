#pragma once

// Minimal HTTP/1.1 server for the local console (`dispatchlab serve`).
// POSIX sockets, loopback interface only, one thread per connection,
// GET/HEAD only, `Connection: close`. No third-party dependencies.

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace dispatchlab {

struct HttpRequest {
    std::string method;
    std::string path;  // decoded, without query string
    std::map<std::string, std::string> query;

    std::string param(const std::string& key, const std::string& def = "") const;
    std::int64_t int_param(const std::string& key, std::int64_t def, std::int64_t lo, std::int64_t hi) const;
};

struct HttpResponse {
    int status = 200;
    std::string content_type = "application/json; charset=utf-8";
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
};

using HttpHandler = std::function<HttpResponse(const HttpRequest&)>;

class HttpServer {
public:
    // Binds 127.0.0.1:<port> (port 0 picks a free port). Throws on failure.
    HttpServer(std::uint16_t port, HttpHandler handler, bool log_requests = true);
    ~HttpServer();
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    std::uint16_t port() const { return port_; }
    void run();  // blocks, serving until stop()
    void stop();

private:
    void serve_connection(int fd);

    int listen_fd_ = -1;
    std::uint16_t port_ = 0;
    HttpHandler handler_;
    bool log_;
    std::atomic<bool> running_{false};
};

std::string url_decode(const std::string& s);
std::map<std::string, std::string> parse_query(const std::string& q);
const char* mime_type_for(const std::string& path);

}  // namespace dispatchlab
