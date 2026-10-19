// Console server load benchmark (plan.md A4): throughput and latency of the
// real HTTP stack under concurrent clients, worker pool vs thread-per-connection.
// Server and clients run in one process on loopback, so they share the CPU;
// numbers describe the whole system on this machine, not the server alone.
//
// usage: benchmark_server <graph> [--seconds 4] [--algo cch] [--out file.json]

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <thread>

#include "bench_common.hpp"
#include "dispatchlab/console_api.hpp"
#include "dispatchlab/http_server.hpp"
#include "dispatchlab/rng.hpp"

using namespace dispatchlab;

namespace {

// One request on a fresh connection; returns true on HTTP 200.
bool request(std::uint16_t port, const std::string& path) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    linger lg{1, 0};  // RST on close: no TIME_WAIT pile-up from thousands of short connections
    ::setsockopt(fd, SOL_SOCKET, SO_LINGER, &lg, sizeof lg);
#ifdef SO_NOSIGPIPE
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
        ::close(fd);
        return false;
    }
    const std::string req = "GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
    if (::send(fd, req.data(), req.size(), 0) != static_cast<ssize_t>(req.size())) {
        ::close(fd);
        return false;
    }
    char buf[4096];
    std::string head;
    ssize_t k;
    while ((k = ::recv(fd, buf, sizeof buf, 0)) > 0) {
        if (head.size() < 32) head.append(buf, static_cast<std::size_t>(k));
    }
    ::close(fd);
    return head.rfind("HTTP/1.1 200", 0) == 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);
    bench::Args args(argc, argv);
    const double seconds = static_cast<double>(args.num("seconds", 4));
    const std::string algo = args.get("algo", "cch");
    const std::string out = args.get("out", "");
    const std::vector<unsigned> client_counts = {1, 4, 16, 64};
    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());

    ConsoleOptions opts;
    opts.static_root = "viewer";
    ConsoleApi api(load_graph(args.graph), opts);
    const std::uint32_t n = api.graph().num_nodes();

    json::Writer w;
    w.begin_object();
    w.kv("benchmark", "server");
    bench::write_environment(w, api.graph(), 0);
    w.kv("endpoint", "/api/distance?algo=" + algo).kv("seconds_per_cell", seconds).kv("hardware_threads", hw);
    w.kv("methodology",
         "in-process server and client threads on loopback; each request opens a new TCP connection "
         "(Connection: close), queries a random (s,t) pair, reads the full response; latency = connect to EOF");
    w.key("results").begin_array();
    std::printf("server benchmark: /api/distance?algo=%s, %u-node graph, %.0f s per cell, %u hardware threads\n",
                algo.c_str(), n, seconds, hw);
    std::printf("%-26s %8s %12s %10s %10s %10s %8s\n", "mode", "clients", "req/s", "p50_us", "p95_us", "p99_us", "errors");

    for (const unsigned workers : {hw, 0u}) {
        const std::string mode = workers ? "worker pool (" + std::to_string(workers) + ")" : "thread per connection";
        HttpServer server(0, [&api](const HttpRequest& r) { return api.handle(r); }, false, workers);
        std::thread srv([&] { server.run(); });
        const std::uint16_t port = server.port();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        request(port, "/api/distance?s=0&t=1&algo=" + algo);  // build the traffic context

        for (const unsigned clients : client_counts) {
            std::atomic<bool> go{true};
            std::vector<std::vector<double>> lat(clients);
            std::vector<std::uint64_t> errors(clients, 0);
            const std::uint64_t shed0 = server.shed_connections();
            std::vector<std::thread> pool;
            for (unsigned c = 0; c < clients; ++c) {
                pool.emplace_back([&, c] {
                    Rng rng(1000 + c);
                    while (go.load(std::memory_order_relaxed)) {
                        const std::string path = "/api/distance?s=" + std::to_string(rng.below(n)) + "&t=" +
                                                 std::to_string(rng.below(n)) + "&algo=" + algo;
                        const auto t0 = bench::Clock::now();
                        const bool ok = request(port, path);
                        const double us = bench::us_since(t0);
                        if (ok) lat[c].push_back(us);
                        else ++errors[c];
                    }
                });
            }
            std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
            go = false;
            for (auto& t : pool) t.join();
            std::vector<double> all;
            std::uint64_t err = 0;
            for (unsigned c = 0; c < clients; ++c) {
                all.insert(all.end(), lat[c].begin(), lat[c].end());
                err += errors[c];
            }
            const double rps = static_cast<double>(all.size()) / seconds;
            std::printf("%-26s %8u %12.0f %10.0f %10.0f %10.0f %8llu\n", mode.c_str(), clients, rps,
                        percentile(all, 50), percentile(all, 95), percentile(all, 99),
                        static_cast<unsigned long long>(err));
            w.begin_object().kv("mode", mode).kv("workers", workers).kv("clients", clients);
            w.kv("requests_per_s", rps).kv("p50_us", percentile(all, 50)).kv("p95_us", percentile(all, 95));
            w.kv("p99_us", percentile(all, 99)).kv("errors", err);
            w.kv("shed", server.shed_connections() - shed0).end_object();
        }
        server.stop();
        srv.join();
    }
    w.end_array();
    w.end_object();
    if (!out.empty()) json::write_file(out, w.str());
    return 0;
}
