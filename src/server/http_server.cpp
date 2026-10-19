#include "dispatchlab/http_server.hpp"
#include "dispatchlab/json.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace dispatchlab {

namespace {

constexpr std::size_t kMaxRequestBytes = 16 * 1024;

const char* reason(int status) {
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        default: return "Unknown";
    }
}

bool send_all(int fd, const char* data, std::size_t len) {
    while (len > 0) {
#ifdef MSG_NOSIGNAL
        const ssize_t n = ::send(fd, data, len, MSG_NOSIGNAL);
#else
        const ssize_t n = ::send(fd, data, len, 0);
#endif
        if (n <= 0) return false;
        data += n;
        len -= static_cast<std::size_t>(n);
    }
    return true;
}

}  // namespace

std::string HttpRequest::param(const std::string& key, const std::string& def) const {
    auto it = query.find(key);
    return it == query.end() ? def : it->second;
}

std::int64_t HttpRequest::int_param(const std::string& key, std::int64_t def, std::int64_t lo, std::int64_t hi) const {
    auto it = query.find(key);
    if (it == query.end() || it->second.empty()) return def;
    char* end = nullptr;
    const long long v = std::strtoll(it->second.c_str(), &end, 10);
    if (!end || *end != '\0') throw std::invalid_argument("parameter '" + key + "' must be an integer");
    if (v < lo || v > hi) {
        throw std::invalid_argument("parameter '" + key + "' must be between " + std::to_string(lo) + " and " +
                                    std::to_string(hi));
    }
    return v;
}

std::string url_decode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') {
            out.push_back(' ');
        } else if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
                   std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            out.push_back(static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16)));
            i += 2;
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

std::map<std::string, std::string> parse_query(const std::string& q) {
    std::map<std::string, std::string> out;
    std::size_t start = 0;
    while (start <= q.size()) {
        const std::size_t amp = q.find('&', start);
        const std::string part = q.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
        if (!part.empty()) {
            const std::size_t eq = part.find('=');
            if (eq == std::string::npos) out[url_decode(part)] = "";
            else out[url_decode(part.substr(0, eq))] = url_decode(part.substr(eq + 1));
        }
        if (amp == std::string::npos) break;
        start = amp + 1;
    }
    return out;
}

const char* mime_type_for(const std::string& path) {
    auto ends = [&](const char* ext) {
        const std::size_t n = std::strlen(ext);
        return path.size() >= n && path.compare(path.size() - n, n, ext) == 0;
    };
    if (ends(".html")) return "text/html; charset=utf-8";
    if (ends(".css")) return "text/css; charset=utf-8";
    if (ends(".js") || ends(".mjs")) return "text/javascript; charset=utf-8";
    if (ends(".json")) return "application/json; charset=utf-8";
    if (ends(".svg")) return "image/svg+xml";
    if (ends(".woff2")) return "font/woff2";
    if (ends(".png")) return "image/png";
    if (ends(".txt") || ends(".md")) return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

HttpServer::HttpServer(std::uint16_t port, HttpHandler handler, bool log_requests)
    : handler_(std::move(handler)), log_(log_requests) {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) throw std::runtime_error("socket() failed");
    int one = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // loopback only: never reachable from the network
    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
        ::close(listen_fd_);
        throw std::runtime_error("cannot bind 127.0.0.1:" + std::to_string(port) + " (port in use?)");
    }
    if (::listen(listen_fd_, 64) != 0) {
        ::close(listen_fd_);
        throw std::runtime_error("listen() failed");
    }
    socklen_t len = sizeof addr;
    ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len);
    port_ = ntohs(addr.sin_port);
}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::stop() {
    running_ = false;
    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
}

void HttpServer::run() {
    running_ = true;
    while (running_) {
        const int fd = ::accept(listen_fd_, nullptr, nullptr);
        if (fd < 0) {
            if (!running_) break;
            continue;
        }
#ifdef SO_NOSIGPIPE
        int one = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
        std::thread([this, fd] { serve_connection(fd); }).detach();
    }
}

void HttpServer::serve_connection(int fd) {
    const auto t0 = std::chrono::steady_clock::now();
    std::string buf;
    char chunk[4096];
    while (buf.find("\r\n\r\n") == std::string::npos && buf.size() < kMaxRequestBytes) {
        const ssize_t n = ::recv(fd, chunk, sizeof chunk, 0);
        if (n <= 0) break;
        buf.append(chunk, static_cast<std::size_t>(n));
    }

    HttpResponse res;
    HttpRequest req;
    const std::size_t line_end = buf.find("\r\n");
    if (buf.size() >= kMaxRequestBytes) {
        res.status = 413;
        res.body = R"({"error":"request too large"})";
    } else if (line_end == std::string::npos) {
        res.status = 400;
        res.body = R"({"error":"malformed request"})";
    } else {
        const std::string line = buf.substr(0, line_end);
        const std::size_t sp1 = line.find(' ');
        const std::size_t sp2 = line.find(' ', sp1 == std::string::npos ? 0 : sp1 + 1);
        if (sp1 == std::string::npos || sp2 == std::string::npos) {
            res.status = 400;
            res.body = R"({"error":"malformed request line"})";
        } else {
            req.method = line.substr(0, sp1);
            const std::string target = line.substr(sp1 + 1, sp2 - sp1 - 1);
            const std::size_t qm = target.find('?');
            req.path = url_decode(target.substr(0, qm));
            if (qm != std::string::npos) req.query = parse_query(target.substr(qm + 1));
            if (req.method != "GET" && req.method != "HEAD") {
                res.status = 405;
                res.body = R"({"error":"only GET is supported"})";
            } else {
                try {
                    res = handler_(req);
                } catch (const std::invalid_argument& e) {
                    res = HttpResponse{};
                    res.status = 400;
                    res.body = "{\"error\":\"" + json::escape(e.what()) + "\"}";
                } catch (const std::exception& e) {
                    res = HttpResponse{};
                    res.status = 500;
                    res.body = "{\"error\":\"" + json::escape(e.what()) + "\"}";
                }
            }
        }
    }

    std::string head = "HTTP/1.1 " + std::to_string(res.status) + " " + reason(res.status) + "\r\n";
    head += "Content-Type: " + res.content_type + "\r\n";
    head += "Content-Length: " + std::to_string(res.body.size()) + "\r\n";
    head += "Connection: close\r\nX-Content-Type-Options: nosniff\r\n";
    for (const auto& [k, v] : res.headers) head += k + ": " + v + "\r\n";
    head += "\r\n";
    if (send_all(fd, head.data(), head.size()) && req.method != "HEAD") send_all(fd, res.body.data(), res.body.size());
    ::shutdown(fd, SHUT_WR);
    ::close(fd);
    if (log_) {
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::fprintf(stderr, "%s %s %d %.1fms %zuB\n", req.method.empty() ? "-" : req.method.c_str(),
                     req.path.empty() ? "-" : req.path.c_str(), res.status, ms, res.body.size());
    }
}

}  // namespace dispatchlab
