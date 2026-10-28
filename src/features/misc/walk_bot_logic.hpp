#pragma once

// Decisions of the walk bot that do not touch the game: which enemy to chase, which random point to walk to, whether
// a reload can be finished unseen, how a key sequence is spelled, and how a recorded route is stored. Kept free of game
// and Windows state so the tests can drive them directly.

#include <foundation/math/vector.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace features::misc::walk_logic
{
// ---------------------------------------------------------------------------------------------------------------------
// Headway: whether the feet actually get anywhere. Speed alone misses counter-strafing on one spot, where the velocity
// flips back and forth and stays high while the player goes nowhere.

class headway
{
public:
    using clock = std::chrono::steady_clock;
    // How long the feet have stayed within `radius` (on the ground plane) of one spot while keys were pressed.
    clock::duration update(const foundation::vec3& feet, const clock::time_point now, const bool pressing,
                           const float radius = 96.0f)
    {
        const auto dx = feet.x - m_anchor.x;
        const auto dy = feet.y - m_anchor.y;
        if (!m_valid || !pressing || dx * dx + dy * dy > radius * radius)
        {
            m_anchor = feet;
            m_since = now;
            m_valid = true;
        }
        return now - m_since;
    }
    void reset() noexcept { m_valid = false; }

private:
    foundation::vec3 m_anchor{};
    clock::time_point m_since{};
    bool m_valid{};
};

// ---------------------------------------------------------------------------------------------------------------------
// Hunt: which enemy to run at.

struct hunt_candidate
{
    std::uintptr_t id{};
    float distance{}; // how far away (route length when known, else straight)
};

// Keeps the current target unless another one is clearly closer: closer than `switch_ratio` of the current target's
// distance. Two enemies on opposite sides at similar distances therefore never make the bot swing back and forth.
[[nodiscard]] inline std::uintptr_t choose_hunt_target(std::span<const hunt_candidate> candidates,
                                                       const std::uintptr_t current,
                                                       const float switch_ratio = 0.5f) noexcept
{
    const hunt_candidate* best = nullptr;
    const hunt_candidate* kept = nullptr;
    for (const auto& candidate : candidates)
    {
        if (!candidate.id || !std::isfinite(candidate.distance))
            continue;
        if (!best || candidate.distance < best->distance)
            best = &candidate;
        if (candidate.id == current)
            kept = &candidate;
    }
    if (!best)
        return 0;
    if (kept && kept != best && !(best->distance < kept->distance * switch_ratio))
        return kept->id;
    return best->id;
}

// ---------------------------------------------------------------------------------------------------------------------
// Random points: where to walk next.

struct destination_rules
{
    float min_distance{900.0f};  // straight-line distance to a candidate
    float max_distance{2600.0f}; // beyond this a walk turns into a lap across the map
    float recent_radius{700.0f}; // around a recently reached point
    float behind_penalty{1.0f};  // weight against turning back (0..1 of the score range)
};

// Score of a candidate point; empty when the point is not acceptable at all. Higher is better. `heading` is the
// player's recent walking direction (zero when unknown); `noise` is a random value in [0,1) that keeps choices varied.
[[nodiscard]] inline std::optional<float> destination_score(const foundation::vec3& player,
                                                            const foundation::vec3& heading,
                                                            const foundation::vec3& candidate,
                                                            std::span<const foundation::vec3> recent, const float noise,
                                                            const destination_rules& rules = {}) noexcept
{
    const auto dx = candidate.x - player.x;
    const auto dy = candidate.y - player.y;
    const auto distance = std::hypot(dx, dy);
    if (!std::isfinite(distance) || distance < rules.min_distance || distance > rules.max_distance)
        return std::nullopt;
    for (const auto& point : recent)
        if (std::hypot(candidate.x - point.x, candidate.y - point.y) < rules.recent_radius)
            return std::nullopt;
    float score = noise;
    const auto heading_length = std::hypot(heading.x, heading.y);
    if (heading_length > 0.1f)
    {
        // 1 straight ahead, 0 sideways, -1 straight back: turning back costs the most.
        const auto ahead = (dx * heading.x + dy * heading.y) / (distance * heading_length);
        score += rules.behind_penalty * 0.5f * (ahead - 1.0f);
    }
    return score;
}

// A route this much longer than the straight line is a detour around half the map: rejected in favour of another
// point.
[[nodiscard]] inline bool route_is_direct(const float route_length, const float straight_distance) noexcept
{
    if (!std::isfinite(route_length) || route_length <= 0.0f)
        return false;
    return route_length <= std::max(straight_distance * 2.2f, straight_distance + 900.0f);
}

// ---------------------------------------------------------------------------------------------------------------------
// Auto reload: can the reload finish before anyone can shoot?

struct threat
{
    foundation::vec3 eye{};
    foundation::vec3 velocity{};
};

// Samples the reload time: the player follows `own_eye_at(t)` (the route), each enemy keeps its velocity. Unsafe as
// soon as any enemy could see the player at any sample. `visible(a, b)` is a line-of-sight test.
[[nodiscard]] inline bool
reload_window_safe(const std::function<foundation::vec3(float)>& own_eye_at, std::span<const threat> threats,
                   const float reload_seconds,
                   const std::function<bool(const foundation::vec3&, const foundation::vec3&)>& visible,
                   const float step = 0.2f)
{
    if (!(reload_seconds > 0.0f) || !(step > 0.0f))
        return true;
    for (float t = 0.0f; t <= reload_seconds + 1e-4f; t += step)
    {
        const auto own = own_eye_at(t);
        for (const auto& enemy : threats)
        {
            const auto position = enemy.eye + enemy.velocity * t;
            if (visible(own, position))
                return false;
        }
    }
    return true;
}

// Whether the magazine is low enough to want a reload: under half, and not already full.
[[nodiscard]] inline bool wants_reload(const int clip, const int max_clip) noexcept
{
    return max_clip > 1 && clip >= 0 && clip * 2 < max_clip;
}

// ---------------------------------------------------------------------------------------------------------------------
// Key sequences: "B 4 2 B" -> virtual keys.

// Windows virtual-key code of one token: a letter or digit, F1..F24, or a named key. 0 for an unknown token.
[[nodiscard]] inline std::uint16_t key_from_token(std::string_view token) noexcept
{
    std::string upper;
    upper.reserve(token.size());
    for (const auto c : token)
        upper.push_back(static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c));
    if (upper.size() == 1)
    {
        const auto c = upper[0];
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
            return static_cast<std::uint16_t>(c);
        return 0;
    }
    if (upper.size() >= 2 && upper[0] == 'F')
    {
        int number = 0;
        for (std::size_t i = 1; i < upper.size(); ++i)
        {
            if (upper[i] < '0' || upper[i] > '9')
                return 0;
            number = number * 10 + (upper[i] - '0');
        }
        return number >= 1 && number <= 24 ? static_cast<std::uint16_t>(0x70 + number - 1) : 0;
    }
    struct named
    {
        std::string_view name;
        std::uint16_t key;
    };
    static constexpr named k_names[]{{"ENTER", 0x0D}, {"ESC", 0x1B},   {"ESCAPE", 0x1B},   {"SPACE", 0x20},
                                     {"TAB", 0x09},   {"SHIFT", 0x10}, {"CTRL", 0x11},     {"ALT", 0x12},
                                     {"NUM0", 0x60},  {"NUM1", 0x61},  {"NUM2", 0x62},     {"NUM3", 0x63},
                                     {"NUM4", 0x64},  {"NUM5", 0x65},  {"NUM6", 0x66},     {"NUM7", 0x67},
                                     {"NUM8", 0x68},  {"NUM9", 0x69},  {"BACKSPACE", 0x08}};
    for (const auto& entry : k_names)
        if (upper == entry.name)
            return entry.key;
    return 0;
}

// Splits on spaces and commas. Unknown tokens make the whole sequence invalid (empty), so a typo never presses a
// different key than intended.
[[nodiscard]] inline std::vector<std::uint16_t> parse_key_sequence(std::string_view text)
{
    std::vector<std::uint16_t> keys;
    std::size_t at = 0;
    while (at < text.size())
    {
        while (at < text.size() && (text[at] == ' ' || text[at] == ',' || text[at] == '\t'))
            ++at;
        const auto start = at;
        while (at < text.size() && text[at] != ' ' && text[at] != ',' && text[at] != '\t')
            ++at;
        if (start == at)
            continue;
        const auto key = key_from_token(text.substr(start, at - start));
        if (!key)
            return {};
        keys.push_back(key);
    }
    return keys;
}

// Buying. A preset lists items by their buy-menu code (the digits pressed in the menu: "42" is the second rifle,
// "25" the fifth pistol-slot... whatever the game's wheel shows), each for one side or both and with a money
// condition. Every item that applies is bought in one visit to the menu (armour and a gun), or one of them at random.

struct buy_entry
{
    bool enabled{};
    std::string_view code{};
    int side{};       // 0 both, 1 T, 2 CT
    int money_rule{}; // 0 any amount, 1 less than `money`, 2 at least `money`
    int money{};
};

// One or two digits 1-9: the category and the item in it.
[[nodiscard]] inline bool buy_code_valid(const std::string_view code) noexcept
{
    if (code.empty() || code.size() > 2)
        return false;
    return std::ranges::all_of(code, [](const char c) { return c >= '1' && c <= '9'; });
}

// team: 2 T, 3 CT.
[[nodiscard]] inline bool buy_entry_applies(const buy_entry& entry, const int team, const int money) noexcept
{
    if (!entry.enabled || !buy_code_valid(entry.code))
        return false;
    if ((entry.side == 1 && team != 2) || (entry.side == 2 && team != 3))
        return false;
    if (entry.money_rule == 1 && money >= entry.money)
        return false;
    if (entry.money_rule == 2 && money < entry.money)
        return false;
    return true;
}

// The keys to press: the menu key, the codes of the items that apply (or one of them, picked by `pick` in [0, 1)),
// the menu key again to close it. Empty when nothing applies.
[[nodiscard]] inline std::vector<std::uint16_t> buy_keys(std::span<const buy_entry> entries, const int team,
                                                         const int money, const bool random_one, const float pick,
                                                         const std::uint16_t menu_key)
{
    std::vector<const buy_entry*> chosen;
    for (const auto& entry : entries)
        if (buy_entry_applies(entry, team, money))
            chosen.push_back(&entry);
    if (chosen.empty() || !menu_key)
        return {};
    if (random_one)
    {
        const auto index = std::min(static_cast<std::size_t>(std::max(pick, 0.0f) * static_cast<float>(chosen.size())),
                                    chosen.size() - 1);
        chosen = {chosen[index]};
    }
    std::vector<std::uint16_t> keys{menu_key};
    for (const auto* entry : chosen)
        for (const auto c : entry->code)
            keys.push_back(static_cast<std::uint16_t>(c));
    keys.push_back(menu_key);
    return keys;
}

// Deathmatch: the buy menu works while the respawn protection lasts and the player has not moved. Whatever the
// random-weapons setting, a player who spawned with a main weapon already has one (the game hands the last bought gun
// out again, or a random one); a pistol-only spawn (the first life after warmup, say) is when to buy.
[[nodiscard]] inline bool deathmatch_should_buy(const bool immune, const bool moved, const bool has_main_weapon)
{
    return immune && !moved && !has_main_weapon;
}

// The weapon types that count as a main weapon (CSWeaponType: SMG, rifle, shotgun, sniper rifle, machine gun).
[[nodiscard]] constexpr bool main_weapon_type(const std::uint32_t type) noexcept
{
    return type >= 2 && type <= 6;
}

// ---------------------------------------------------------------------------------------------------------------------
// Recorded routes.

struct recorded_point
{
    enum : std::uint8_t
    {
        jump = 1,
        duck = 2,
        use = 4,
    };
    foundation::vec3 position{};
    std::uint8_t flags{};
};

struct recording
{
    std::string map{};
    int team{};
    std::vector<recorded_point> points{};
};

// A point is kept when the player has moved this far from the last kept one, or when the jump/duck/use state changes.
[[nodiscard]] inline bool should_record(const recorded_point& last, const recorded_point& next,
                                        const float spacing = 24.0f) noexcept
{
    const auto moved = std::hypot(next.position.x - last.position.x, next.position.y - last.position.y);
    return moved >= spacing || std::abs(next.position.z - last.position.z) >= 24.0f || next.flags != last.flags;
}

// Plain text: a header line, then one point per line ("x y z flags"). Readable and diff-friendly.
[[nodiscard]] inline std::string serialize(const recording& value)
{
    std::ostringstream out;
    out.precision(7);
    out << "vesta-route 1 " << value.map << ' ' << value.team << ' ' << value.points.size() << '\n';
    for (const auto& point : value.points)
        out << point.position.x << ' ' << point.position.y << ' ' << point.position.z << ' '
            << static_cast<int>(point.flags) << '\n';
    return out.str();
}

[[nodiscard]] inline std::optional<recording> parse_recording(std::string_view text)
{
    std::istringstream in{std::string{text}};
    std::string magic;
    int version{};
    recording value{};
    std::size_t count{};
    if (!(in >> magic >> version >> value.map >> value.team >> count) || magic != "vesta-route" || version != 1 ||
        count > 100000)
        return std::nullopt;
    value.points.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        recorded_point point{};
        int flags{};
        if (!(in >> point.position.x >> point.position.y >> point.position.z >> flags) || flags < 0 || flags > 7 ||
            !std::isfinite(point.position.x) || !std::isfinite(point.position.y) || !std::isfinite(point.position.z))
            return std::nullopt;
        point.flags = static_cast<std::uint8_t>(flags);
        value.points.push_back(point);
    }
    if (value.points.size() < 2)
        return std::nullopt;
    return value;
}
} // namespace features::misc::walk_logic
