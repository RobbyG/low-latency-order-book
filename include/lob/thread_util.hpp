#pragma once

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <sched.h>

namespace lob {

inline void pause_cpu() noexcept {
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(__aarch64__)
    __asm__ volatile("yield" ::: "memory");
#else
#error "Unsupported architecture for pause_cpu"
#endif
}

[[nodiscard]] inline int try_pin_to_core(int core_id) noexcept {
    if (core_id < 0 || core_id >= CPU_SETSIZE) {
        return EINVAL;
    }
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}

inline void pin_to_core_or_die(int core_id) noexcept {
    const int rc = try_pin_to_core(core_id);
    if (rc != 0) {
        std::fprintf(stderr, "pthread_setaffinity_np failed for core %d: returned %d\n", core_id,
                     rc);
        std::abort();
    }
}

} // namespace lob