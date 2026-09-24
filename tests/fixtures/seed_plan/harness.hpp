#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <utility>
#include <simulation/shot_model.hpp>
#include <simulation/recoil_model.hpp>
#include <simulation/seed_window.hpp>
#include <simulation/seed_timing.hpp>
namespace foundation { inline float wrap_yaw(float value) noexcept { return std::remainder(value,360.0f); } }

namespace fixture {
inline foundation::vec3 view{};
inline float fraction{};
inline foundation::vec3 punch{};
inline bool punch_readable{true};
inline bool readable{true};
inline int failed_punch_tick{-1};
inline std::array<simulation::shot_model::shot_time,2> sampled_times{};
inline unsigned timed_reads{};
inline foundation::vec3 future_punch{};
inline std::optional<std::array<simulation::shot_model::recoil_state,2>> native_recoil;
}
namespace simulation {
inline std::optional<seed_window::recoil_pair> read_recoil_state(std::uintptr_t) {
    if (!fixture::punch_readable) return {};
    if (fixture::native_recoil)
        return seed_window::recoil_pair{(*fixture::native_recoil)[0], (*fixture::native_recoil)[1]};
    const shot_model::recoil_state predicted{{100, 0.0f}, fixture::punch, {}};
    const shot_model::recoil_state unpredicted{{100, 0.0f}, {}, {}};
    return seed_window::recoil_pair{predicted, unpredicted};
}
inline std::optional<float> read_seed_fraction() { return fixture::fraction; }
inline std::optional<foundation::vec3> read_shot_punch(std::uintptr_t, shot_model::shot_time time) {
    fixture::sampled_times[fixture::timed_reads++ % 2]=time;
    if (!fixture::punch_readable || time.tick==fixture::failed_punch_tick) return {};
    if (fixture::native_recoil) {
        foundation::vec3 sum{};
        for (const auto& state : *fixture::native_recoil) {
            shot_model::recoil_cache cache; foundation::vec3 value{};
            if (!cache.sample(state,time,value)) return {};
            sum+=value;
        }
        return sum;
    }
    return fixture::punch + (time.fraction==0 ? fixture::future_punch : foundation::vec3{});
}
}
namespace app {
struct reader {
    template<class T> T load(std::uintptr_t address) { T value{}; copy(address, &value, sizeof(value)); return value; }
    bool copy(std::uintptr_t, void* out, std::size_t size) {
        if (!fixture::readable || size != sizeof(fixture::view)) return false;
        std::memcpy(out, &fixture::view, size);
        return true;
    }
};
struct state { reader process; };
inline state& context() { static state value; return value; }
}
#define SCHEMA(...) 16
namespace features::aimbot {
inline float seed_quantize_angle(float angle) { return simulation::shot_model::quantize(angle); }
inline std::optional<foundation::vec3> read_seed_punch(std::uintptr_t) {
    if (!fixture::punch_readable) return {};
    return fixture::punch;
}
inline std::optional<foundation::vec3> read_seed_punch(std::uintptr_t pawn, int tick) {
    return simulation::read_shot_punch(pawn, {tick, 0});
}
class aimbot_t {
public:
    enum class seed_prediction_phase : std::uint8_t { current, ambiguous, next };
    struct seed_shot_sample {
        int tick{};
        foundation::vec3 hash_angles{}, direction_angles{};
        float fraction{};
        foundation::vec3 punch{};
    };
    struct seed_shot_plan {
        seed_prediction_phase phase{};
        seed_shot_sample current{}, next{};
        std::uint64_t source_key{};
        int source_tick{-1};
        foundation::vec3 source_angles{}, prepared_punch{};
    };
    struct seed_angle_history_entry { int tick{-1}; foundation::vec3 hash_angles{}; };
    std::optional<seed_shot_plan> build_seed_plan(std::uintptr_t, const foundation::vec3&,
        bool, int, std::chrono::steady_clock::time_point, bool);
    static bool possible_seed_match(const seed_shot_plan&, const std::pair<bool,bool>&);
    static bool safe_seed_match(const seed_shot_plan&, const std::pair<bool,bool>&);
    std::array<seed_angle_history_entry,3> m_seed_angle_history{};
    std::size_t m_seed_angle_history_count{};
    int m_seed_phase_tick{-1}, m_seed_memo_sequence{-1};
    std::chrono::steady_clock::time_point m_seed_phase_tick_at{};
    float m_seed_memo_pitch{},m_seed_memo_yaw{},m_seed_memo_next_pitch{},m_seed_memo_next_yaw{};
    seed_prediction_phase m_seed_memo_phase{};
    foundation::vec3 m_seed_memo_direction{},m_seed_memo_next_direction{};
};
}
