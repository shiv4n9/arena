#pragma once

#include <cstdint>
#include <chrono>
#include <thread>

#ifdef _WIN32
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

namespace arctic {

// rdtscp clock wrapper
// we don't trust the OS for time. reads cycles straight from the cpu.
// rdtscp forces instruction serialization so the cpu doesn't reorder it and mess up our timings.
class TscClock {
public:
    TscClock() {
        calibrate();
    }

    // grab ticks (< 10ns)
    static inline uint64_t rdtscp() {
        unsigned int aux;
        return __rdtscp(&aux);
    }

    // ticks + core id so we know if we crossed numa nodes
    static inline uint64_t rdtscp(unsigned int& core_id) {
        return __rdtscp(&core_id);
    }

    // old school rdtsc with lfence
    static inline uint64_t rdtsc_fenced() {
        _mm_lfence();
        return __rdtsc();
    }

    // ticks to ns
    double ticks_to_ns(uint64_t ticks) const {
        return static_cast<double>(ticks) / ticks_per_ns_;
    }

    // ticks to secs
    double ticks_to_seconds(uint64_t ticks) const {
        return static_cast<double>(ticks) / (ticks_per_ns_ * 1e9);
    }

    double get_ticks_per_ns() const { return ticks_per_ns_; }
    double get_estimated_freq_ghz() const { return ticks_per_ns_; }

private:
    double ticks_per_ns_;

    // figure out how fast the cpu is actually running
    // we take the median of a few sleeps to ignore OS context switches
    void calibrate() {
        constexpr int calibration_rounds = 5;
        constexpr auto calibration_duration = std::chrono::milliseconds(50);
        double measurements[calibration_rounds];

        for (int i = 0; i < calibration_rounds; ++i) {
            auto wall_start = std::chrono::steady_clock::now();
            uint64_t tsc_start = rdtscp();

            std::this_thread::sleep_for(calibration_duration);

            uint64_t tsc_end = rdtscp();
            auto wall_end = std::chrono::steady_clock::now();

            double wall_ns = std::chrono::duration<double, std::nano>(wall_end - wall_start).count();
            double tsc_delta = static_cast<double>(tsc_end - tsc_start);
            measurements[i] = tsc_delta / wall_ns;
        }

        // median filter
        for (int i = 0; i < calibration_rounds - 1; ++i) {
            for (int j = i + 1; j < calibration_rounds; ++j) {
                if (measurements[j] < measurements[i]) {
                    double tmp = measurements[i];
                    measurements[i] = measurements[j];
                    measurements[j] = tmp;
                }
            }
        }
        ticks_per_ns_ = measurements[calibration_rounds / 2];
    }
};

} // namespace arctic
