#pragma once

// Shared helpers for the benchmark executables.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "dispatchlab/graph.hpp"
#include "dispatchlab/json.hpp"
#include "dispatchlab/sysinfo.hpp"

namespace bench {

using Clock = std::chrono::steady_clock;

inline double us_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
}

struct Args {
    std::string graph;
    std::map<std::string, std::string> opts;
    Args(int argc, char** argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::strncmp(argv[i], "--", 2) == 0 && i + 1 < argc) {
                opts[argv[i] + 2] = argv[i + 1];
                ++i;
            } else {
                graph = argv[i];
            }
        }
        if (graph.empty()) {
            std::fprintf(stderr, "usage: %s <graph> [--out file.json] [--seed S] [...]\n", argv[0]);
            std::exit(2);
        }
    }
    std::string get(const std::string& k, const std::string& d) const {
        auto it = opts.find(k);
        return it == opts.end() ? d : it->second;
    }
    std::uint64_t num(const std::string& k, std::uint64_t d) const {
        auto it = opts.find(k);
        return it == opts.end() ? d : std::strtoull(it->second.c_str(), nullptr, 10);
    }
};

inline void write_environment(dispatchlab::json::Writer& w, const dispatchlab::Graph& g, std::uint64_t seed) {
    const dispatchlab::SysInfo si = dispatchlab::system_info();
    w.key("environment").begin_object();
    w.kv("cpu", si.cpu).kv("os", si.os).kv("logical_cores", si.logical_cores).kv("memory_bytes", si.memory_bytes);
    w.kv("compiler", si.compiler).kv("build_type", si.build_type).kv("cxx_flags", si.cxx_flags);
    w.kv("timestamp_utc", si.timestamp_utc);
    w.end_object();
    w.key("dataset").begin_object();
    w.kv("source", g.meta.source).kv("nodes", g.num_nodes()).kv("edges", g.num_edges());
    w.kv("max_speed_kmh", g.meta.max_speed_kmh);
    w.end_object();
    w.kv("seed", seed);
}

// Prevents the optimizer from discarding benchmark results.
template <class T>
inline void do_not_optimize(const T& v) {
    asm volatile("" : : "r,m"(v) : "memory");
}

}  // namespace bench
