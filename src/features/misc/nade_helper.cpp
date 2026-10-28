#include <stdafx.hpp>
#include <foundation/identity.hpp>
#include <game/state/constants.hpp>
#include <platform/windows/input.hpp>
#include <render/presentation.hpp>
#include <config/settings.hpp>
#include <config/misc.hpp>
#include <game/state/pose.hpp>
#include <game/state/services.hpp>
#include <game/memory/bound_services.hpp>
#include <features/misc/misc.hpp>
#include <game/state/level.hpp>
#include <render/text/localization.hpp>
#include <resources/nades/nade_data.hpp>

#include <string>
#include <string_view>

namespace features::misc
{

bool nade_helper_t::gameplay_allows_throw(const std::uintptr_t pawn)
{
    if (!pawn || game::bound_memory().process.load<bool>(pawn + SCHEMA("C_CSPlayerPawn", "m_bIsBuyMenuOpen"_id)))
    {
        return false;
    }

    auto& game_rules = m_game_rules;
    const auto now = std::chrono::steady_clock::now();
    if (now >= m_next_rules_scan)
    {
        m_next_rules_scan = now + std::chrono::milliseconds(500);
        game_rules = 0;
        const auto entities = game::entity_index().all();
        for (const auto& entity : *entities)
        {
            if (entity.schema_id != "C_CSGameRulesProxy"_id)
                continue;
            game_rules = game::bound_memory().process.load<std::uintptr_t>(
                entity.ptr + SCHEMA("C_CSGameRulesProxy", "m_pGameRules"_id));
            if (game_rules)
                break;
        }
    }

    if (!game_rules)
        return true;
    return !game::bound_memory().process.load<bool>(game_rules + SCHEMA("C_CSGameRules", "m_bFreezePeriod"_id)) &&
           !game::bound_memory().process.load<bool>(game_rules + SCHEMA("C_CSGameRules", "m_bTeamIntroPeriod"_id)) &&
           !game::bound_memory().process.load<bool>(game_rules + SCHEMA("C_GameRules", "m_bGamePaused"_id));
}

namespace
{

namespace nd = resources::nades;

// The map-name probe can return anything from a bare identifier to a full
// vpk path, so reduce whatever it found to the plain level name.
[[nodiscard]] std::string_view normalize_map(std::string_view raw)
{
    if (const auto slash = raw.find_last_of("/\\"); slash != std::string_view::npos)
    {
        raw.remove_prefix(slash + 1);
    }

    if (const auto dot = raw.find('.'); dot != std::string_view::npos)
    {
        raw = raw.substr(0, dot);
    }

    return raw;
}

[[nodiscard]] const nd::map_entries* find_map(std::string_view name)
{
    for (const auto& candidate : nd::k_maps)
    {
        if (name == candidate.map)
        {
            return &candidate;
        }
    }

    return nullptr;
}

// Angular distance between the current view and a lineup's stored angles.
[[nodiscard]] float angle_error(const foundation::vec3& view_angles, float pitch, float yaw)
{
    const auto dx = pitch - view_angles.x;
    const auto dy = foundation::wrap_yaw(yaw - view_angles.y);
    return std::sqrt(dx * dx + dy * dy);
}

[[nodiscard]] const char* button_name(const float strength)
{
    if (strength <= 0.25f)
        return render::localization::tr("RMB");
    if (strength < 0.75f)
        return render::localization::tr("LMB+RMB");
    return render::localization::tr("LMB");
}

[[nodiscard]] std::string translate_action(std::string_view action)
{
    if (action.empty())
    {
        return {};
    }

    const auto whole = render::localization::tr(action);
    if (whole != action)
    {
        return std::string{whole};
    }

    std::string out{};
    std::size_t start{0};
    while (start <= action.size())
    {
        const auto plus = action.find('+', start);
        const auto token = action.substr(start, plus == std::string_view::npos ? std::string_view::npos : plus - start);

        if (!out.empty())
        {
            out += '+';
        }
        out += render::localization::tr(token);

        if (plus == std::string_view::npos)
        {
            break;
        }
        start = plus + 1;
    }

    return out;
}

[[nodiscard]] foundation::vec3 local_origin(std::uintptr_t pawn)
{
    const auto node =
        game::bound_memory().process.load<std::uintptr_t>(pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_id));
    return node ? game::bound_memory().process.load<foundation::vec3>(node +
                                                                      SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_id))
                : foundation::vec3{};
}

[[nodiscard]] bool game_has_input_focus()
{
    const auto foreground = ::GetForegroundWindow();
    const auto root = foreground ? ::GetAncestor(foreground, GA_ROOT) : nullptr;
    DWORD process_id{};
    if (root)
        ::GetWindowThreadProcessId(root, &process_id);
    return process_id != 0 && process_id == game::bound_memory().process.process_id();
}


} // namespace

std::uint8_t nade_helper_t::resolve_kind(std::uintptr_t weapon_vdata)
{
    constexpr auto unknown = static_cast<std::uint8_t>(0xff);
    if (!weapon_vdata)
    {
        return unknown;
    }

    const auto name_ptr =
        game::bound_memory().process.load<std::uintptr_t>(weapon_vdata + SCHEMA("CCSWeaponBaseVData", "m_szName"_id));
    if (!name_ptr)
    {
        return unknown;
    }

    char name[64]{};
    if (!game::bound_memory().process.copy(name_ptr, name, sizeof(name) - 1))
    {
        return unknown;
    }

    switch (foundation::identity::of(name))
    {
        case "weapon_smokegrenade"_id:
            return static_cast<std::uint8_t>(nd::kind::smoke);
        case "weapon_flashbang"_id:
            return static_cast<std::uint8_t>(nd::kind::flash);
        case "weapon_molotov"_id:
        case "weapon_incgrenade"_id:
            return static_cast<std::uint8_t>(nd::kind::molotov);
        case "weapon_hegrenade"_id:
            return static_cast<std::uint8_t>(nd::kind::he);
        case "weapon_decoy"_id:
            return static_cast<std::uint8_t>(nd::kind::decoy);
        default:
            return unknown;
    }
}

std::string nade_helper_t::throw_instruction(const lineup_view& lineup)
{
    std::string keys{};

    const auto append = [&keys](const char* name)
    {
        if (!keys.empty())
        {
            keys += '+';
        }

        keys += render::localization::tr(name);
    };

    if (lineup.actions & nd::action_crouch)
    {
        append("Crouch");
    }

    if (lineup.actions & nd::action_walk)
    {
        append("Walk");
    }

    if (lineup.actions & nd::action_run)
    {
        append("Forward");
    }

    if (lineup.actions & nd::action_jump)
    {
        append("Jump");
    }

    // vformat, not format: the format string is a translation looked up at
    // runtime, so it cannot be a compile-time checked literal.
    const std::string button{button_name(lineup.throw_strength)};

    if (keys.empty())
    {
        return std::vformat(render::localization::tr("Hold {}, release"), std::make_format_args(button));
    }

    return std::vformat(render::localization::tr("Hold {}, {}, release"), std::make_format_args(button, keys));
}

bool nade_helper_t::collect(const foundation::vec3& player_pos, std::vector<lineup_view>& out) const
{
    const auto runtime_settings = config::get_runtime_snapshot();
    out.clear();

    const auto kind = resolve_kind(game::local_player().weapon_vdata());
    if (kind == 0xff)
    {
        return false;
    }

    const auto map_name = game::level().name();
    const auto map = map_name ? find_map(normalize_map(*map_name)) : nullptr;
    if (!map)
    {
        return false;
    }

    const auto& cfg = runtime_settings->general.m_nade_helper;
    const auto max_distance_sqr = cfg.draw_distance * cfg.draw_distance;

    for (std::uint32_t i = 0; i < map->count; ++i)
    {
        const auto& entry = map->data[i];
        if (static_cast<std::uint8_t>(entry.type) != kind)
        {
            continue;
        }

        const foundation::vec3 position{entry.x, entry.y, entry.z};
        const auto distance_sqr = position.distance_sqr(player_pos);
        if (distance_sqr > max_distance_sqr)
        {
            continue;
        }

        out.push_back(lineup_view{
            .name = entry.name,
            .action = entry.action,
            .position = position,
            .pitch = entry.pitch,
            .yaw = entry.yaw,
            .actions = entry.actions,
            .run_ticks = entry.run_ticks,
            .after_jump_ticks = entry.after_jump_ticks,
            .throw_strength = entry.throw_strength,
            .manual = entry.manual,
            .distance = std::sqrt(distance_sqr),
        });
    }

    // Nearest last: painters order, so the closest plaque ends up on top.
    std::sort(out.begin(), out.end(),
              [](const lineup_view& a, const lineup_view& b) { return a.distance > b.distance; });

    return !out.empty();
}

int nade_helper_t::select_armed(const std::vector<lineup_view>& lineups, const foundation::vec3& view_angles) const
{
    const auto runtime_settings = config::get_runtime_snapshot();
    const auto& cfg = runtime_settings->general.m_nade_helper;

    auto best_index{-1};
    auto best_error = std::numeric_limits<float>::max();

    for (std::size_t i = 0; i < lineups.size(); ++i)
    {
        const auto& lineup = lineups[i];
        if (lineup.distance > cfg.stand_radius)
        {
            continue;
        }

        const auto error = angle_error(view_angles, lineup.pitch, lineup.yaw);
        if (error < best_error)
        {
            best_error = error;
            best_index = static_cast<int>(i);
        }
    }

    return best_index;
}

bool nade_helper_t::execution_position_ready(const lineup_view& lineup, const foundation::vec3& player_pos) const
{
    const auto runtime_settings = config::get_runtime_snapshot();
    const auto& cfg = runtime_settings->general.m_nade_helper;
    const auto dx = lineup.position.x - player_pos.x;
    const auto dy = lineup.position.y - player_pos.y;
    return dx * dx + dy * dy <= cfg.release_radius * cfg.release_radius &&
           std::abs(lineup.position.z - player_pos.z) <= cfg.height_tolerance;
}

void nade_helper_t::reset_lock()
{
    m_lock_started = {};
    m_lock_name = nullptr;
    m_lock_position = {};
    m_lock_pitch = 0.0f;
    m_lock_yaw = 0.0f;
}

void nade_helper_t::on_render(render::draw::draw_list& draw_list)
{
    const auto runtime_settings = config::get_runtime_snapshot();
    const auto& cfg = runtime_settings->general.m_nade_helper;
    if (!cfg.enabled || !game::local_player().alive())
    {
        return;
    }

    const auto pawn = game::local_player().pawn();
    if (!pawn)
    {
        return;
    }

    const auto player_pos = local_origin(pawn);
    if (!collect(player_pos, m_render_scratch))
    {
        return;
    }

    const auto eye_pos = game::camera().origin();
    auto view_angles = game::camera().angles();
    auto unused_origin = foundation::vec3{};
    static_cast<void>(game::camera().sample(unused_origin, view_angles));
    const auto armed_index = select_armed(m_render_scratch, view_angles);

    // Once the player is standing on a spot, everything else on the map is
    // noise -- they have already chosen. Draw only what belongs to this spot.
    const auto occupied = armed_index >= 0;

    for (std::size_t i = 0; i < m_render_scratch.size(); ++i)
    {
        const auto& lineup = m_render_scratch[i];
        const auto in_zone = lineup.distance <= cfg.stand_radius;

        if (occupied && !in_zone)
        {
            continue;
        }

        if (in_zone)
        {
            const auto selected = static_cast<int>(i) == armed_index;
            const auto converged = execution_position_ready(lineup, player_pos) &&
                                   angle_error(view_angles, lineup.pitch, lineup.yaw) <= cfg.aim_threshold;

            draw_stand_marker(draw_list, lineup, true);
            draw_aim_guidance(draw_list, lineup, eye_pos, selected, converged);
            continue;
        }

        if (lineup.distance <= cfg.stand_distance)
        {
            draw_stand_marker(draw_list, lineup, false);
        }

        const auto screen = game::camera().project(lineup.position + foundation::vec3{0.0f, 0.0f, 16.0f});
        if (game::camera().projection_valid(screen))
        {
            draw_plaque(draw_list, lineup, screen);
        }
    }
}

void nade_helper_t::draw_text_plaque(render::draw::draw_list& draw_list, float center_x, float top_y,
                                     std::string_view title, std::string_view subtitle,
                                     const render::draw::rgba& accent, float alpha) const
{
    const auto runtime_settings = config::get_runtime_snapshot();
    const auto& cfg = runtime_settings->general.m_nade_helper;
    const auto& fonts = render::fonts();
    const auto* title_font = fonts.menu_semibold_13;
    const auto* sub_font = fonts.menu_regular_12;

    if (!title_font || alpha <= 0.01f)
    {
        return;
    }

    const auto scale_alpha = [alpha](const render::draw::rgba& color)
    {
        return render::draw::rgba{
            color.r, color.g, color.b,
            static_cast<std::uint8_t>(std::clamp(static_cast<float>(color.a) * alpha, 0.0f, 255.0f))};
    };

    const auto [title_w, title_h] = render::draw::measure_text(title, title_font);
    auto sub_w{0.0f};
    auto sub_h{0.0f};
    if (!subtitle.empty() && sub_font)
    {
        const auto measured = render::draw::measure_text(subtitle, sub_font);
        sub_w = measured.first;
        sub_h = measured.second;
    }

    constexpr auto pad_x{8.0f};
    constexpr auto pad_y{5.0f};
    constexpr auto gap{1.0f};

    const auto content_w = std::max(title_w, sub_w);
    const auto content_h = title_h + (sub_h > 0.0f ? gap + sub_h : 0.0f);
    const auto box_w = content_w + pad_x * 2.0f;
    const auto box_h = content_h + pad_y * 2.0f;
    const auto x = center_x - box_w * 0.5f;
    const auto y = top_y;

    if (auto* im = draw_list.m_im_draw_list)
    {
        im->AddRectFilled({x, y}, {x + box_w, y + box_h},
                          render::draw::draw_list::to_im_color(scale_alpha(cfg.plaque_background)), 4.0f);

        // A thin accent edge keeps the plaque readable against bright walls.
        im->AddLine({x, y + 3.0f}, {x, y + box_h - 3.0f}, render::draw::draw_list::to_im_color(scale_alpha(accent)),
                    2.0f);
    }

    draw_list.add_text(x + pad_x, y + pad_y, title, title_font, scale_alpha(cfg.plaque_text));

    if (!subtitle.empty() && sub_font)
    {
        const render::draw::rgba dimmed{cfg.plaque_text.r, cfg.plaque_text.g, cfg.plaque_text.b,
                                        static_cast<std::uint8_t>(cfg.plaque_text.a * 0.62f)};
        draw_list.add_text(x + pad_x, y + pad_y + title_h + gap, subtitle, sub_font, scale_alpha(dimmed));
    }
}

void nade_helper_t::draw_plaque(render::draw::draw_list& draw_list, const lineup_view& lineup,
                                const foundation::vec2& screen) const
{
    const auto runtime_settings = config::get_runtime_snapshot();
    const auto& cfg = runtime_settings->general.m_nade_helper;

    std::string subtitle{};
    if (cfg.show_action)
    {
        subtitle = translate_action(lineup.action);
    }

    if (cfg.show_distance)
    {
        if (!subtitle.empty())
        {
            subtitle += "  ";
        }

        // 52 units to the metre. The unit suffix is translated; the number is not.
        subtitle += std::format("{:.0f}{}", lineup.distance / 52.0f, render::localization::tr("m"));
    }

    // Fade out over the last third of the draw range so distant spots do not
    // pile up into an unreadable wall of text.
    const auto fade_start = cfg.draw_distance * 0.66f;
    const auto fade =
        lineup.distance <= fade_start
            ? 1.0f
            : std::clamp(1.0f - (lineup.distance - fade_start) / std::max(cfg.draw_distance - fade_start, 1.0f), 0.0f,
                         1.0f);

    // The world point is the anchor's bottom, so lift the box above it.
    const auto [_, title_h] = render::draw::measure_text(lineup.name, render::fonts().menu_semibold_13);
    draw_text_plaque(draw_list, screen.x, screen.y - (title_h + 26.0f), lineup.name, subtitle, cfg.plaque_accent, fade);
}

void nade_helper_t::draw_stand_marker(render::draw::draw_list& draw_list, const lineup_view& lineup,
                                      bool standing) const
{
    const auto runtime_settings = config::get_runtime_snapshot();
    const auto& cfg = runtime_settings->general.m_nade_helper;
    const auto color = standing ? cfg.stand_marker_active : cfg.stand_marker;

    constexpr auto segments{28};
    std::vector<float> points{};
    points.reserve(segments * 2);

    for (int i = 0; i < segments; ++i)
    {
        const auto theta = (static_cast<float>(i) / static_cast<float>(segments)) * 2.0f * std::numbers::pi_v<float>;
        const foundation::vec3 world{
            lineup.position.x + std::cos(theta) * cfg.stand_radius,
            lineup.position.y + std::sin(theta) * cfg.stand_radius,
            lineup.position.z + 1.0f,
        };

        const auto screen = game::camera().project(world);
        if (!game::camera().projection_valid(screen))
        {
            return;
        }

        points.push_back(screen.x);
        points.push_back(screen.y);
    }

    draw_list.add_polyline(points, color, true, standing ? 2.0f : 1.4f);

    const auto center = game::camera().project(lineup.position + foundation::vec3{0.0f, 0.0f, 1.0f});
    if (game::camera().projection_valid(center))
    {
        draw_list.add_circle_filled(center.x, center.y, standing ? 2.5f : 1.8f, color, 10);
    }
}

void nade_helper_t::draw_aim_guidance(render::draw::draw_list& draw_list, const lineup_view& lineup,
                                      const foundation::vec3& eye_pos, bool selected, bool converged) const
{
    const auto runtime_settings = config::get_runtime_snapshot();
    const auto& cfg = runtime_settings->general.m_nade_helper;

    foundation::vec3 forward{};
    const foundation::vec3 angles{lineup.pitch, lineup.yaw, 0.0f};
    angles.to_directions(&forward, nullptr, nullptr);

    const auto screen = game::camera().project(eye_pos + forward * k_aim_marker_distance);
    if (!game::camera().projection_valid(screen))
    {
        return;
    }

    // Alternatives sharing this stand point stay as a bare dot: enough to show
    // another lineup exists, not enough to compete with the chosen one.
    if (!selected)
    {
        draw_list.add_circle(screen.x, screen.y, 4.0f, cfg.aim_marker, 16, 1.0f);
        return;
    }

    const auto color = converged ? cfg.stand_marker_active : cfg.aim_marker;
    const auto radius = converged ? 7.0f : 5.5f;
    const auto thickness = converged ? 2.0f : 1.4f;

    draw_list.add_circle(screen.x, screen.y, radius, color, 20, thickness);
    draw_list.add_line(screen.x - radius - 4.0f, screen.y, screen.x - radius - 1.0f, screen.y, color, thickness);
    draw_list.add_line(screen.x + radius + 1.0f, screen.y, screen.x + radius + 4.0f, screen.y, color, thickness);
    draw_list.add_line(screen.x, screen.y - radius - 4.0f, screen.x, screen.y - radius - 1.0f, color, thickness);
    draw_list.add_line(screen.x, screen.y + radius + 1.0f, screen.x, screen.y + radius + 4.0f, color, thickness);

    const auto instruction =
        lineup.manual ? std::string{render::localization::tr("Manual lineup")} : throw_instruction(lineup);

    if (!converged)
    {
        const auto [screen_w, screen_h] = render::draw::get_display_size();
        const auto center_x = static_cast<float>(screen_w) * 0.5f;
        const auto center_y = static_cast<float>(screen_h) * 0.5f;

        const render::draw::rgba trail{cfg.aim_marker.r, cfg.aim_marker.g, cfg.aim_marker.b,
                                       static_cast<std::uint8_t>(cfg.aim_marker.a * 0.55f)};
        draw_list.add_line(center_x, center_y, screen.x, screen.y, trail, 1.4f);

        draw_text_plaque(draw_list, screen.x, screen.y + radius + 10.0f, lineup.name, instruction, cfg.plaque_accent,
                         1.0f);
        return;
    }

    // On target: the plaque has done its job and would only cover the very spot
    // being aimed at, so drop back to a plain label under the marker.
    const auto* font = render::fonts().menu_regular_12;
    if (!font)
    {
        return;
    }

    const auto name_size = render::draw::measure_text(lineup.name, font);
    draw_list.add_text(screen.x - name_size.first * 0.5f, screen.y + radius + 8.0f, lineup.name, font, cfg.plaque_text,
                       render::draw::text_style::outlined);

    const auto hint_size = render::draw::measure_text(instruction, font);
    const render::draw::rgba hint_color{cfg.stand_marker_active.r, cfg.stand_marker_active.g, cfg.stand_marker_active.b,
                                        cfg.plaque_text.a};
    draw_list.add_text(screen.x - hint_size.first * 0.5f, screen.y + radius + 8.0f + name_size.second + 2.0f,
                       instruction, font, hint_color, render::draw::text_style::outlined);
}

void nade_helper_t::tick()
{
    if (m_release_pending)
    {
        cancel_throw(m_activation_latched);
        if (m_release_pending)
            return;
    }
    const auto runtime_settings = config::get_runtime_snapshot();
    if (!runtime_settings)
    {
        cancel_throw(false);
        return;
    }
    const auto& cfg = runtime_settings->general.m_nade_helper;
    if (!cfg.enabled)
    {
        const auto owns_input = m_throw_phase != throw_phase::idle || !m_gated_keys.empty() || m_forward.pressed ||
                                m_walk.pressed || m_duck.pressed || m_jump.pressed || m_attack.pressed ||
                                m_attack2.pressed;
        if (owns_input)
            cancel_throw(false);
        else
        {
            m_activation_latched = false;
            reset_lock();
        }
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    const auto activation_held = input_lifecycle::activation_held(
        cfg.aim_key, [](const std::uint16_t key) { return platform::windows::bound_input().physical_key_down(key); });
    if (!activation_held)
    {
        const auto owns_input = m_throw_phase != throw_phase::idle || !m_gated_keys.empty() || m_forward.pressed ||
                                m_walk.pressed || m_duck.pressed || m_jump.pressed || m_attack.pressed ||
                                m_attack2.pressed;
        if (owns_input)
            cancel_throw(false);
        m_activation_latched = false;
        m_aim_error = {};
        m_aim_velocity = {};
        m_last_aim_update = {};
        reset_lock();
        return;
    }

    const auto pawn = game::local_player().pawn();
    const auto controller = game::local_player().controller();
    const auto weapon = game::local_player().weapon();
    const auto weapon_vdata = game::local_player().weapon_vdata();
    const auto weapon_type = weapon_vdata ? game::bound_memory().process.load<std::uint32_t>(
                                                weapon_vdata + SCHEMA("CCSWeaponBaseVData", "m_WeaponType"_id))
                                          : std::uint32_t{};
    const auto usable = !render::menu_open() && !game::input_bindings().text_entry_active() && game_has_input_focus() &&
                        controller && pawn && game::local_player().alive() && weapon &&
                        weapon_type == game::rules::equipment_class::throwable && gameplay_allows_throw(pawn);
    if (!usable || (m_throw_phase != throw_phase::idle && (pawn != m_active_pawn || weapon != m_active_weapon)))
    {
        cancel_throw(activation_held);
        m_aim_error = {};
        m_aim_velocity = {};
        return;
    }

    const auto raw_tick =
        game::bound_memory().process.load<std::int32_t>(controller + SCHEMA("CBasePlayerController", "m_nTickBase"_id));
    if (raw_tick <= 0)
    {
        cancel_throw(activation_held);
        return;
    }
    const auto tick = static_cast<std::uint32_t>(raw_tick);
    if (m_throw_phase != throw_phase::idle)
    {
        if (cfg.aim_assist)
        {
            foundation::vec3 unused_origin{}, live_view{};
            if (!game::camera().sample(unused_origin, live_view))
            {
                cancel_throw(activation_held);
                return;
            }
            auto error = 0.0f;
            aim_at(m_active_lineup, live_view, error);
        }
        drive_throw(pawn, weapon, tick, now);
        return;
    }

    if (!activation_held || m_activation_latched)
    {
        m_aim_error = {};
        m_aim_velocity = {};
        m_last_aim_update = {};
        return;
    }
    const auto player_pos = local_origin(pawn);
    if (!collect(player_pos, m_tick_scratch))
    {
        reset_lock();
        return;
    }

    foundation::vec3 unused_origin{}, view_angles{};
    if (!game::camera().sample(unused_origin, view_angles))
    {
        reset_lock();
        return;
    }
    const auto index = select_armed(m_tick_scratch, view_angles);
    if (index < 0)
    {
        m_aim_error = {};
        m_aim_velocity = {};
        m_last_aim_update = {};
        reset_lock();
        return;
    }

    const auto& lineup = m_tick_scratch[static_cast<std::size_t>(index)];
    auto error = angle_error(view_angles, lineup.pitch, lineup.yaw);
    if (cfg.aim_assist)
        aim_at(lineup, view_angles, error);

    foundation::vec3 velocity{};
    if (!game::bound_memory().process.copy(pawn + SCHEMA("C_BaseEntity", "m_vecAbsVelocity"_id), &velocity,
                                           sizeof(velocity)))
    {
        reset_lock();
        return;
    }
    const auto stationary = std::isfinite(velocity.x) && std::isfinite(velocity.y) && std::isfinite(velocity.z) &&
                            velocity.length_2d() <= 12.0f && std::abs(velocity.z) <= 12.0f;
    const auto position_ready = execution_position_ready(lineup, player_pos);
    const auto lock_matches = m_lock_name == lineup.name && m_lock_position.distance_sqr(lineup.position) <= 0.01f &&
                              std::abs(m_lock_pitch - lineup.pitch) <= 0.001f &&
                              std::abs(foundation::wrap_yaw(m_lock_yaw - lineup.yaw)) <= 0.001f;
    if (position_ready && stationary && error <= cfg.aim_threshold)
    {
        if (!lock_matches)
        {
            m_lock_name = lineup.name;
            m_lock_position = lineup.position;
            m_lock_pitch = lineup.pitch;
            m_lock_yaw = lineup.yaw;
            m_lock_started = now;
        }
    }
    else
    {
        reset_lock();
    }

    const auto settled = m_lock_started != std::chrono::steady_clock::time_point{} &&
                         now - m_lock_started >= std::chrono::milliseconds(std::clamp(cfg.lock_time_ms, 0, 250));
    if (cfg.auto_release && !lineup.manual && settled)
    {
        if (!begin_throw(lineup, pawn, weapon, tick, now))
            m_activation_latched = true;
    }
}

bool nade_helper_t::set_control(owned_control& control, const bool pressed)
{
    return input_lifecycle::transition(
        control, pressed,
        [](const game::input_binding& binding, const bool pressed)
        {
            using platform::windows::pointer_action;
            auto success = false;
            switch (binding.device)
            {
                case game::input_device::keyboard:
                    success = platform::windows::bound_input().key(binding.virtual_key, pressed);
                    break;
                case game::input_device::mouse_primary:
                    success = platform::windows::bound_input().pointer(
                        0, 0, pressed ? pointer_action::primary_down : pointer_action::primary_up);
                    break;
                case game::input_device::mouse_secondary:
                    success = platform::windows::bound_input().pointer(
                        0, 0, pressed ? pointer_action::secondary_down : pointer_action::secondary_up);
                    break;
                case game::input_device::mouse_middle:
                    success = platform::windows::bound_input().pointer(
                        0, 0, pressed ? pointer_action::middle_down : pointer_action::middle_up);
                    break;
                case game::input_device::mouse_auxiliary1:
                    success = platform::windows::bound_input().pointer(
                        0, 0, pressed ? pointer_action::auxiliary1_down : pointer_action::auxiliary1_up);
                    break;
                case game::input_device::mouse_auxiliary2:
                    success = platform::windows::bound_input().pointer(
                        0, 0, pressed ? pointer_action::auxiliary2_down : pointer_action::auxiliary2_up);
                    break;
                default:
                    break;
            }
            return success;
        });
}

void nade_helper_t::release_movement(const bool include_jump)
{
    if (include_jump)
        (void)set_control(m_jump, false);
    (void)set_control(m_forward, false);
    (void)set_control(m_walk, false);
    (void)set_control(m_duck, false);
}

void nade_helper_t::release_attacks()
{
    using platform::windows::pointer_action;
    const auto primary_mouse = m_attack.pressed && m_attack.binding.device == game::input_device::mouse_primary;
    const auto secondary_mouse = m_attack2.pressed && m_attack2.binding.device == game::input_device::mouse_secondary;
    if (primary_mouse && secondary_mouse &&
        platform::windows::bound_input().pointer(0, 0, pointer_action::primary_up | pointer_action::secondary_up))
    {
        m_attack.pressed = false;
        m_attack2.pressed = false;
    }
    (void)set_control(m_attack, false);
    (void)set_control(m_attack2, false);
}

void nade_helper_t::cancel_throw(const bool latch)
{
    release_attacks();
    release_movement();
    const std::array controls{&m_forward, &m_walk, &m_duck, &m_jump, &m_attack, &m_attack2};
    m_release_pending = !input_lifecycle::discard_released(controls);
    if (!m_release_pending)
    {
        platform::windows::bound_input().set_movement_gate(platform::windows::input_gateway::gate_owner::nade_helper,
                                                           {}, false);
        m_gated_keys.clear();
    }
    m_active_lineup = {};
    m_active_pawn = 0;
    m_active_weapon = 0;
    m_throw_phase = throw_phase::idle;
    m_movement_taken.store(false, std::memory_order_release);
    m_phase_tick = 0;
    m_run_start_tick = 0;
    m_jump_tick = 0;
    m_aim_error = {};
    m_aim_velocity = {};
    m_last_aim_update = {};
    reset_lock();
    m_activation_latched = latch;
}

bool nade_helper_t::begin_throw(const lineup_view& lineup, const std::uintptr_t pawn, const std::uintptr_t weapon,
                                const std::uint32_t tick, const std::chrono::steady_clock::time_point now)
{
    cancel_throw(false);
    if (m_release_pending)
        return false;
    m_active_lineup = lineup;
    m_active_pawn = pawn;
    m_active_weapon = weapon;
    m_phase_tick = tick;
    m_phase_started = now;

    auto& bindings = game::input_bindings();
    if (lineup.actions & nd::action_run)
        m_forward.binding = bindings.resolve(game::input_action::forward);
    if (lineup.actions & nd::action_walk)
        m_walk.binding = bindings.resolve(game::input_action::walk);
    if (lineup.actions & nd::action_crouch)
        m_duck.binding = bindings.resolve(game::input_action::duck);
    if (lineup.actions & nd::action_jump)
        m_jump.binding = bindings.resolve(game::input_action::jump);

    if (lineup.throw_strength >= 0.75f)
        m_attack.binding = bindings.resolve(game::input_action::attack);
    else if (lineup.throw_strength <= 0.25f)
        m_attack2.binding = bindings.resolve(game::input_action::attack2);
    else
    {
        m_attack.binding = bindings.resolve(game::input_action::attack);
        m_attack2.binding = bindings.resolve(game::input_action::attack2);
    }

    constexpr std::array movement_actions{
        game::input_action::forward, game::input_action::back, game::input_action::left, game::input_action::right,
        game::input_action::walk,    game::input_action::duck, game::input_action::jump,
    };
    for (const auto action : movement_actions)
    {
        for (const auto& binding : bindings.candidates(action))
        {
            if (binding.device != game::input_device::keyboard || !binding.virtual_key ||
                std::ranges::find(m_gated_keys, binding.virtual_key) != m_gated_keys.end())
            {
                continue;
            }
            m_gated_keys.push_back(binding.virtual_key);
        }
    }
    if (!m_gated_keys.empty())
        platform::windows::bound_input().set_movement_gate(platform::windows::input_gateway::gate_owner::nade_helper,
                                                           m_gated_keys, true);
    m_movement_taken.store(true, std::memory_order_release);

    const auto missing =
        ((lineup.actions & nd::action_run) && !m_forward.binding) ||
        ((lineup.actions & nd::action_walk) && !m_walk.binding) ||
        ((lineup.actions & nd::action_crouch) && !m_duck.binding) ||
        ((lineup.actions & nd::action_jump) && !m_jump.binding) ||
        (lineup.throw_strength >= 0.75f && !m_attack.binding) ||
        (lineup.throw_strength <= 0.25f && !m_attack2.binding) ||
        (lineup.throw_strength > 0.25f && lineup.throw_strength < 0.75f && (!m_attack.binding || !m_attack2.binding));
    if (missing)
    {
        cancel_throw(true);
        return false;
    }

    if (lineup.actions & nd::action_crouch)
    {
        if (!set_control(m_duck, true))
        {
            cancel_throw(true);
            return false;
        }
        m_throw_phase = throw_phase::crouching;
        return true;
    }
    if (!prime_throw(tick, now))
    {
        cancel_throw(true);
        return false;
    }
    return true;
}

bool nade_helper_t::prime_throw(const std::uint32_t tick, const std::chrono::steady_clock::time_point now)
{
    const auto strength = m_active_lineup.throw_strength;
    if (strength >= 0.75f)
    {
        if (!set_control(m_attack, true))
            return false;
    }
    else if (strength <= 0.25f)
    {
        if (!set_control(m_attack2, true))
            return false;
    }
    else if (m_attack.binding.device == game::input_device::mouse_primary &&
             m_attack2.binding.device == game::input_device::mouse_secondary)
    {
        using platform::windows::pointer_action;
        if (!platform::windows::bound_input().pointer(0, 0,
                                                      pointer_action::primary_down | pointer_action::secondary_down))
        {
            return false;
        }
        m_attack.pressed = true;
        m_attack2.pressed = true;
    }
    else if (!set_control(m_attack, true) || !set_control(m_attack2, true))
    {
        return false;
    }

    m_throw_phase = throw_phase::priming;
    m_phase_tick = tick;
    m_phase_started = now;
    return true;
}

void nade_helper_t::finish_throw(const std::uint32_t tick)
{
    const auto release_after = (m_active_lineup.actions & nd::action_release_movement_after_throw) != 0;
    if (release_after)
    {
        release_attacks();
        release_movement(false);
    }
    else
    {
        release_movement(false);
        release_attacks();
    }

    m_activation_latched = true;
    m_phase_tick = tick;
    if (m_active_lineup.actions & nd::action_jump)
        m_throw_phase = throw_phase::complete;
    else
        cancel_throw(true);
}

void nade_helper_t::drive_throw(const std::uintptr_t pawn, const std::uintptr_t weapon, const std::uint32_t tick,
                                const std::chrono::steady_clock::time_point now)
{
    switch (m_throw_phase)
    {
        case throw_phase::crouching:
        {
            const auto movement = game::bound_memory().process.load<std::uintptr_t>(
                pawn + SCHEMA("C_BasePlayerPawn", "m_pMovementServices"_id));
            const auto duck_amount = movement
                                         ? game::bound_memory().process.load<float>(
                                               movement + SCHEMA("CCSPlayer_MovementServices", "m_flDuckAmount"_id))
                                         : 0.0f;
            const auto ducked = movement && (duck_amount >= 0.94f ||
                                             game::bound_memory().process.load<bool>(
                                                 movement + SCHEMA("CCSPlayer_MovementServices", "m_bDucked"_id)));
            if (ducked)
            {
                if (!prime_throw(tick, now))
                    cancel_throw(true);
            }
            else if (now - m_phase_started > std::chrono::milliseconds(900))
                cancel_throw(true);
            return;
        }
        case throw_phase::priming:
        {
            const auto pin_pulled =
                game::bound_memory().process.load<bool>(weapon + SCHEMA("C_BaseCSGrenade", "m_bPinPulled"_id));
            const auto strength =
                game::bound_memory().process.load<float>(weapon + SCHEMA("C_BaseCSGrenade", "m_flThrowStrength"_id));
            const auto ready = pin_pulled && std::isfinite(strength) &&
                               std::abs(strength - m_active_lineup.throw_strength) <= 0.08f && tick != m_phase_tick;
            if (!ready)
            {
                if (now - m_phase_started > std::chrono::milliseconds(3000))
                    cancel_throw(true);
                return;
            }

            if (m_active_lineup.actions & nd::action_run)
            {
                if (!m_forward.pressed)
                {
                    if ((m_active_lineup.actions & nd::action_walk) && !set_control(m_walk, true))
                    {
                        cancel_throw(true);
                        return;
                    }
                    if (!set_control(m_forward, true))
                    {
                        cancel_throw(true);
                        return;
                    }
                    m_run_start_tick = tick;
                }
                m_throw_phase = throw_phase::running;
                if (tick - m_run_start_tick < m_active_lineup.run_ticks)
                    return;
            }
            else if (!(m_active_lineup.actions & nd::action_jump))
            {
                finish_throw(tick);
                return;
            }
            [[fallthrough]];
        }
        case throw_phase::running:
            if (tick - m_run_start_tick < m_active_lineup.run_ticks)
                return;
            if (m_active_lineup.actions & nd::action_jump)
            {
                if (!set_control(m_jump, true))
                {
                    cancel_throw(true);
                    return;
                }
                m_jump_tick = tick;
                m_phase_started = now;
                m_throw_phase = throw_phase::jumping;
            }
            else
                finish_throw(tick);
            return;
        case throw_phase::jumping:
        {
            const auto elapsed_ticks = tick - m_jump_tick;
            const auto after_jump = m_active_lineup.after_jump_ticks;
            if (elapsed_ticks < after_jump)
                return;

            if (after_jump == 0)
            {
                // A distinct worker iteration is the ordering barrier for the classic
                // same-tick jump throw.  No artificial gameplay delay is introduced.
                if (now > m_phase_started)
                    finish_throw(tick);
                return;
            }

            const auto flags =
                game::bound_memory().process.load<std::uint32_t>(pawn + SCHEMA("C_BaseEntity", "m_fFlags"_id));
            const auto velocity = game::bound_memory().process.load<foundation::vec3>(
                pawn + SCHEMA("C_BaseEntity", "m_vecAbsVelocity"_id));
            const auto airborne = (flags & 1u) == 0u || (std::isfinite(velocity.z) && std::abs(velocity.z) > 12.0f);
            if (airborne)
            {
                finish_throw(tick);
                return;
            }

            if (now - m_phase_started > std::chrono::milliseconds(250))
                cancel_throw(true);
            return;
        }
        case throw_phase::complete:
            // Keep +jump asserted through one complete simulation sample after the
            // grenade release. An immediate down/up pair can fall between usercmd polls.
            if (tick != m_phase_tick)
                cancel_throw(true);
            return;
        default:
            return;
    }
}

void nade_helper_t::aim_at(const lineup_view& lineup, const foundation::vec3& view_angles, float& out_error)
{
    const auto runtime_settings = config::get_runtime_snapshot();
    const auto& cfg = runtime_settings->general.m_nade_helper;
    const auto now = std::chrono::steady_clock::now();

    constexpr auto m_yaw{0.022f};
    const auto sensitivity = game::variables().get<float>(CONVAR("sensitivity"_id));
    const auto fov_adjust = game::bound_memory().process.load<float>(
        game::local_player().pawn() + SCHEMA("C_BasePlayerPawn", "m_flFOVSensitivityAdjust"_id));
    const auto deg_per_pixel = sensitivity * m_yaw * fov_adjust;

    if (deg_per_pixel <= 0.0f)
    {
        return;
    }

    auto delta_x = lineup.pitch - view_angles.x;
    auto delta_y = foundation::wrap_yaw(lineup.yaw - view_angles.y);

    out_error = std::sqrt(delta_x * delta_x + delta_y * delta_y);

    const auto dt = m_last_aim_update == std::chrono::steady_clock::time_point{}
                        ? game::rules::simulation_step
                        : std::clamp(std::chrono::duration<float>(now - m_last_aim_update).count(), 0.0005f, 0.05f);
    if (cfg.aim_smoothing > 1)
    {
        const auto response_seconds = (12.0f + static_cast<float>(cfg.aim_smoothing - 1) * 8.0f) * 0.001f;
        const auto desired_pitch = std::clamp(delta_x / response_seconds, -720.0f, 720.0f);
        const auto desired_yaw = std::clamp(delta_y / response_seconds, -720.0f, 720.0f);
        const auto velocity_blend = 1.0f - std::exp(-dt / std::max(0.006f, response_seconds * 0.2f));
        constexpr auto maximum_acceleration{18000.0f};
        const auto maximum_velocity_change = maximum_acceleration * dt;
        const auto approach = [&](float current, float desired)
        {
            const auto blended = current + (desired - current) * velocity_blend;
            return current + std::clamp(blended - current, -maximum_velocity_change, maximum_velocity_change);
        };
        m_aim_velocity.x = approach(m_aim_velocity.x, desired_pitch);
        m_aim_velocity.y = approach(m_aim_velocity.y, desired_yaw);
        delta_x = std::copysign(std::min(std::abs(delta_x), std::abs(m_aim_velocity.x * dt)), delta_x);
        delta_y = std::copysign(std::min(std::abs(delta_y), std::abs(m_aim_velocity.y * dt)), delta_y);
    }
    else
    {
        m_aim_velocity = {};
    }
    m_last_aim_update = now;

    // Sub-pixel remainder is carried between ticks, otherwise the truncation
    // below would stall the aim short of the target on low smoothing values.
    m_aim_error.x += -delta_y / deg_per_pixel;
    m_aim_error.y += delta_x / deg_per_pixel;

    const auto dx = static_cast<int>(m_aim_error.x);
    const auto dy = static_cast<int>(m_aim_error.y);

    m_aim_error.x -= static_cast<float>(dx);
    m_aim_error.y -= static_cast<float>(dy);

    if (dx != 0 || dy != 0)
    {
        platform::windows::bound_input().pointer(dx, dy, platform::windows::pointer_action::relative_move);
    }
}

} // namespace features::misc
