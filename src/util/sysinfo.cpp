#include "dispatchlab/sysinfo.hpp"

#include <sys/resource.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <ctime>
#include <fstream>
#include <string>
#include <thread>

#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

#ifndef DL_COMPILER_ID
#define DL_COMPILER_ID "unknown"
#endif
#ifndef DL_COMPILER_VERSION
#define DL_COMPILER_VERSION "unknown"
#endif
#ifndef DL_BUILD_TYPE
#define DL_BUILD_TYPE "unknown"
#endif
#ifndef DL_CXX_FLAGS
#define DL_CXX_FLAGS ""
#endif

namespace dispatchlab {

namespace {

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\n' || s.back() == '\t')) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

std::string cpu_name() {
#ifdef __APPLE__
    char buf[256];
    std::size_t len = sizeof buf;
    if (sysctlbyname("machdep.cpu.brand_string", buf, &len, nullptr, 0) == 0) return trim(std::string(buf));
#endif
    std::ifstream in("/proc/cpuinfo");
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("model name", 0) == 0) {
            const auto p = line.find(':');
            if (p != std::string::npos) return trim(line.substr(p + 1));
        }
    }
    return "unknown";
}

std::uint64_t total_memory() {
#ifdef __APPLE__
    std::uint64_t mem = 0;
    std::size_t len = sizeof mem;
    if (sysctlbyname("hw.memsize", &mem, &len, nullptr, 0) == 0) return mem;
#endif
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long page = sysconf(_SC_PAGE_SIZE);
    return pages > 0 && page > 0 ? static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(page) : 0;
}

}  // namespace

SysInfo system_info() {
    SysInfo s;
    s.cpu = cpu_name();
    utsname u{};
    if (uname(&u) == 0) s.os = std::string(u.sysname) + " " + u.release + " " + u.machine;
    s.logical_cores = std::thread::hardware_concurrency();
    s.memory_bytes = total_memory();
    s.compiler = std::string(DL_COMPILER_ID) + " " + DL_COMPILER_VERSION;
    s.build_type = DL_BUILD_TYPE;
    s.cxx_flags = trim(DL_CXX_FLAGS);
    char buf[32];
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&now, &tm);
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    s.timestamp_utc = buf;
    return s;
}

std::uint64_t peak_rss_bytes() {
    rusage ru{};
    getrusage(RUSAGE_SELF, &ru);
#ifdef __APPLE__
    return static_cast<std::uint64_t>(ru.ru_maxrss);  // bytes on macOS
#else
    return static_cast<std::uint64_t>(ru.ru_maxrss) * 1024;  // KiB on Linux
#endif
}

}  // namespace dispatchlab
