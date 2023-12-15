#pragma once
#include <QDebug>
#include <chrono>
#include <memory>
#include <functional>

using Qt::StringLiterals::operator""_ba;
using Qt::StringLiterals::operator""_s;
using namespace Qt::Literals::StringLiterals;

template <class ...T>
using Callback = std::function<void(T...)>;

using std::min;
using std::max;
using std::optional;
using std::pair;
using std::array;

using std::move;
using std::forward;


using std::unique_ptr;
using std::make_unique;
using std::shared_ptr;
using std::make_shared;
using std::weak_ptr;


using std::chrono::seconds;
using std::chrono::milliseconds;
using std::chrono::microseconds;
using std::chrono::nanoseconds;
using std::literals::chrono_literals::operator""s;
using std::literals::chrono_literals::operator""ms;

namespace DurationCast {
using std::chrono::round;
using std::chrono::floor;
using std::chrono::ceil;
}

using SteadyTimepoint = std::chrono::time_point<std::chrono::steady_clock>;
constexpr auto SteadyClockMax = std::chrono::steady_clock::time_point::max();
constexpr auto SteadyClockMin = std::chrono::steady_clock::time_point::min();
inline auto SteadyClockNow() {
    return std::chrono::steady_clock::now();
}

constexpr int64_t operator ""_KiB(uint64_t kib) {
    return kib * (1 << 10);
}

constexpr int64_t operator ""_MiB(uint64_t mib) {
    return mib * (1 << 20);
}

constexpr int64_t operator ""_GiB(uint64_t gib) {
    return gib * (1 << 30);
}

constexpr int64_t operator ""_KiB(long double kib) {
    return static_cast<int64_t>(kib * (1 << 10));
}

constexpr int64_t operator ""_MiB(long double mib) {
    return static_cast<int64_t>(mib * (1 << 20));
}

constexpr int64_t operator ""_GiB(long double gib) {
    return static_cast<int64_t>(gib * (1 << 30));
}
