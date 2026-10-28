#include <stdafx.hpp>
#include <foundation/identity.hpp>
#include <game/state/services.hpp>
#include <diagnostics/performance.hpp>
#include <simulation/collision.hpp>
#include <charconv>
#include <game/memory/remote_pointer.hpp>
#include <game/state/model_path.hpp>
#include <diagnostics/journal.hpp>
#include <game/memory/bound_services.hpp>
#include <simulation/collision_layers.hpp>

#include <assets/vpk.hpp>
#include <assets/resource.hpp>
#include <assets/kv3.hpp>

#include <mutex>

namespace simulation
{

namespace
{

struct entity_scan_metrics
{
    std::uint64_t slots{}, seen{}, solids{}, instances{}, incomplete{};
    ~entity_scan_metrics()
    {
        using enum diagnostics::performance::counter;
        diagnostics::performance::record_counter(map_entity_slots, slots);
        diagnostics::performance::record_counter(map_entities_seen, seen);
        diagnostics::performance::record_counter(map_solid_entities, solids);
        diagnostics::performance::record_counter(map_entity_instances, instances);
        diagnostics::performance::record_counter(map_scan_incomplete, incomplete);
    }
};

[[nodiscard]] std::string base_map_name(std::string s)
{
    for (auto& c : s)
    {
        if (c == '\\')
            c = '/';
    }
    if (const auto slash = s.find_last_of('/'); slash != std::string::npos)
    {
        s = s.substr(slash + 1);
    }
    if (const auto dot = s.find_last_of('.'); dot != std::string::npos)
    {
        s.resize(dot);
    }
    return s;
}

[[nodiscard]] const std::vector<std::uint8_t>* as_blob(const assets::kv3::object* o)
{
    if (!o)
    {
        return nullptr;
    }
    return std::get_if<std::vector<std::uint8_t>>(&o->m_value);
}

struct surface_ctx
{
    const std::unordered_map<std::uint32_t, int>* hash_index{};
    const std::vector<collision_world::global_surface_entry>* table{};
    const std::vector<float>* densities{};

    [[nodiscard]] collision_world::surface_info resolve(const std::vector<std::uint32_t>& phys_ides,
                                                        std::int64_t surf_prop_index) const
    {
        collision_world::surface_info s{};

        if (!hash_index || !table || table->empty())
        {
            return s;
        }
        if (surf_prop_index < 0 || static_cast<std::size_t>(surf_prop_index) >= phys_ides.size())
        {
            return s;
        }

        const auto it = hash_index->find(phys_ides[static_cast<std::size_t>(surf_prop_index)]);
        if (it == hash_index->end() || it->second < 0 || static_cast<std::size_t>(it->second) >= table->size())
        {
            return s;
        }

        const auto& e = (*table)[static_cast<std::size_t>(it->second)];
        const auto density = densities && static_cast<std::size_t>(it->second) < densities->size()
                                 ? (*densities)[static_cast<std::size_t>(it->second)]
                                 : 0.0f;
        if (!std::isfinite(e.penetration_mod) || e.penetration_mod <= 0.0f || !std::isfinite(density) ||
            density <= 0.0f)
            return s;
        s.penetration = e.penetration_mod;
        s.surface_type = e.surface_type;
        s.global_index = static_cast<std::uint8_t>(it->second < 255 ? it->second : 255);
        s.density = density;
        return s;
    }
};

[[nodiscard]] bool attribute_is_excluded(const assets::kv3::object* attr)
{
    if (!attr)
    {
        return false;
    }

    const auto lower = [](std::string s)
    {
        for (auto& c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };

    if (const auto* group = attr->find("m_CollisionGroupString"); group && lower(group->as_string()) == "sky")
    {
        return true;
    }

    const auto* interact = attr->find("m_InteractAsStrings");
    if (!interact || !interact->is_array() || interact->size() == 0)
    {
        return false; // no interaction flags -> plain solid surface, keep it
    }

    bool grenade_clip{};
    for (std::size_t i = 0; i < interact->size(); ++i)
    {
        const auto s = lower(interact->get(i).as_string());
        if (s == "csgo_grenadeclip")
            grenade_clip = true;
        else if (s != "playerclip" && s != "npcclip" && s != "sky")
        {
            return false;
        }
    }
    return !grenade_clip;
}

[[nodiscard]] std::uint32_t attribute_contents(const assets::kv3::object* attr)
{
    if (!attr)
        return 0;
    const auto is_grate = [](std::string value)
    {
        for (auto& c : value)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return value.find("grate") != std::string::npos;
    };
    if (const auto* group = attr->find("m_CollisionGroupString"); group && is_grate(group->as_string()))
    {
        return 0x2000;
    }
    if (const auto* interact = attr->find("m_InteractAsStrings"); interact && interact->is_array())
    {
        for (std::size_t index = 0; index < interact->size(); ++index)
        {
            auto value = interact->get(index).as_string();
            for (auto& c : value)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (value == "csgo_grenadeclip")
                return collision_world::grenade_clip_contents;
            if (is_grate(interact->get(index).as_string()))
                return 0x2000;
        }
    }
    return 0;
}

[[nodiscard]] std::uint32_t attribute_interacts_as(const assets::kv3::object* attr)
{
    if (!attr)
        return 0;
    std::uint32_t result{};
    if (const auto* numeric = attr->find("m_nInteractsAs"))
        result = static_cast<std::uint32_t>(numeric->as_int());
    if (const auto* names = attr->find("m_InteractAsStrings"); names && names->is_array())
        for (std::size_t i = 0; i < names->size(); ++i)
            result |= collision_detail::bullet_interaction_layer(names->get(i).as_string());
    return result;
}

[[nodiscard]] foundation::vec3 read_vec3(const std::uint8_t* base, std::int32_t index)
{
    foundation::vec3 result{};
    std::memcpy(&result, base + static_cast<std::size_t>(index) * 12, sizeof(result));
    return result;
}

void append_mesh(const assets::kv3::object* mesh, const collision_world::surface_info& fallback_surface,
                 const surface_ctx& sctx, const std::vector<std::uint32_t>& phys_ides, std::uint64_t solid_id,
                 std::vector<collision_world::triangle>& out)
{
    const auto* vtx = as_blob(mesh ? mesh->find("m_Vertices") : nullptr);
    const auto* tri = as_blob(mesh ? mesh->find("m_Triangles") : nullptr);
    const auto* material_object = mesh ? mesh->find("m_Materials") : nullptr;
    const auto* material_blob = as_blob(material_object);
    const auto has_triangle_materials =
        material_blob ? !material_blob->empty()
                      : material_object && material_object->is_array() && material_object->size() > 0;
    if (!vtx || !tri || vtx->size() < 12 || tri->size() < 12)
    {
        return;
    }

    const auto vertex_count = static_cast<std::int32_t>(vtx->size() / 12);
    const auto index_count = tri->size() / 4;
    const auto index_at = [&](std::size_t index)
    {
        std::int32_t value{};
        std::memcpy(&value, tri->data() + index * 4, sizeof(value));
        return value;
    };
    const auto material_at = [&](std::size_t triangle_index) -> std::optional<std::int64_t>
    {
        if (material_blob && triangle_index < material_blob->size())
            return (*material_blob)[triangle_index];
        if (material_object && material_object->is_array() && triangle_index < material_object->size())
            return material_object->get(triangle_index).as_int();
        return std::nullopt;
    };

    for (std::size_t i = 0; i + 2 < index_count; i += 3)
    {
        const auto triangle_index = i / 3;
        const auto a = index_at(i), b = index_at(i + 1), c = index_at(i + 2);
        if (a < 0 || b < 0 || c < 0 || a >= vertex_count || b >= vertex_count || c >= vertex_count)
        {
            continue;
        }
        auto surface = has_triangle_materials ? collision_world::surface_info{} : fallback_surface;
        surface.contents |= fallback_surface.contents;
        surface.interacts_as |= fallback_surface.interacts_as;
        if (has_triangle_materials)
        {
            if (const auto material_index = material_at(triangle_index))
            {
                auto resolved = sctx.resolve(phys_ides, *material_index);
                if (resolved.global_index != 255)
                {
                    resolved.contents |= fallback_surface.contents;
                    resolved.interacts_as |= fallback_surface.interacts_as;
                    surface = resolved;
                }
            }
        }

        out.push_back({.v0 = read_vec3(vtx->data(), a),
                       .v1 = read_vec3(vtx->data(), b),
                       .v2 = read_vec3(vtx->data(), c),
                       .surface = surface,
                       .solid_id = solid_id});
    }
}

// m_Hull: convex half-edge mesh, fan-triangulated per face. Byte-wide indices
// cap a hull at 256 edges/verts, which holds for CS2's convex pieces.
void append_hull(const assets::kv3::object* hull, const collision_world::surface_info& surface, std::uint64_t solid_id,
                 std::vector<collision_world::triangle>& out)
{
    const auto* pos = as_blob(hull ? hull->find("m_VertexPositions") : nullptr);
    const auto* edges = as_blob(hull ? hull->find("m_Edges") : nullptr);
    const auto* faces = as_blob(hull ? hull->find("m_Faces") : nullptr);
    if (!pos || !edges || !faces || pos->size() < 12)
    {
        return;
    }

    const auto vertex_count = static_cast<std::int32_t>(pos->size() / 12);
    const auto edge_count = edges->size() / 4;
    const auto* edge = edges->data();

    const auto next_of = [&](std::size_t e) { return edge[e * 4 + 0]; };
    const auto origin_of = [&](std::size_t e) { return edge[e * 4 + 2]; };

    for (const auto raw_start : *faces)
    {
        const std::size_t start = raw_start;
        if (start >= edge_count)
        {
            continue;
        }

        std::size_t e = next_of(start);
        int guard = 0;
        while (e != start && guard++ < 256 && e < edge_count)
        {
            const auto o0 = origin_of(start);
            const auto o1 = origin_of(e);
            const auto o2 = origin_of(next_of(e));
            if (o0 < vertex_count && o1 < vertex_count && o2 < vertex_count)
            {
                out.push_back({.v0 = read_vec3(pos->data(), o0),
                               .v1 = read_vec3(pos->data(), o1),
                               .v2 = read_vec3(pos->data(), o2),
                               .surface = surface,
                               .solid_id = solid_id});
            }
            e = next_of(e);
        }
    }
}

struct surface_catalog_data
{
    std::unordered_map<std::uint32_t, int> hash_index{};
    std::vector<collision_world::global_surface_entry> table{};
    std::vector<float> densities{};
};

struct game_surface_override
{
    std::optional<float> penetration{};
    std::optional<float> damage{};
    std::optional<std::uint16_t> material{};
};

[[nodiscard]] std::string lowercase(std::string value)
{
    std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

[[nodiscard]] std::optional<std::string> text_string_value(std::string_view object, std::string_view key)
{
    const auto key_pos = object.find(key);
    if (key_pos == std::string_view::npos)
        return std::nullopt;
    const auto equals = object.find('=', key_pos + key.size());
    if (equals == std::string_view::npos)
        return std::nullopt;
    const auto quote = object.find('"', equals + 1);
    if (quote == std::string_view::npos)
        return std::nullopt;
    const auto end = object.find('"', quote + 1);
    if (end == std::string_view::npos)
        return std::nullopt;
    return std::string(object.substr(quote + 1, end - quote - 1));
}

[[nodiscard]] std::optional<float> text_float_value(std::string_view object, std::string_view key)
{
    const auto key_pos = object.find(key);
    if (key_pos == std::string_view::npos)
        return std::nullopt;
    const auto equals = object.find('=', key_pos + key.size());
    if (equals == std::string_view::npos)
        return std::nullopt;
    auto first = object.find_first_not_of(" \t\r\n", equals + 1);
    if (first == std::string_view::npos)
        return std::nullopt;
    auto last = first;
    while (last < object.size() &&
           (std::isdigit(static_cast<unsigned char>(object[last])) || object[last] == '+' || object[last] == '-' ||
            object[last] == '.' || object[last] == 'e' || object[last] == 'E'))
        ++last;
    float value{};
    const auto* begin = object.data() + first;
    const auto [end, error] = std::from_chars(begin, object.data() + last, value);
    if (error != std::errc{} || end == begin)
        return std::nullopt;
    return value;
}

[[nodiscard]] std::unordered_map<std::string, game_surface_override>
read_game_surface_overrides(assets::vpk_archive& pak)
{
    std::unordered_map<std::string, game_surface_override> result{};
    const auto* entry = pak.find("scripts/surfaceproperties_game.txt");
    if (!entry)
        return result;
    const auto bytes = pak.read(*entry);
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    const auto list = text.find("SurfacePropertiesList");
    const auto begin = list == std::string_view::npos ? std::string_view::npos : text.find('[', list);
    if (begin == std::string_view::npos)
        return result;

    std::size_t object_begin{};
    int depth{};
    bool quoted{};
    for (std::size_t i = begin + 1; i < text.size(); ++i)
    {
        const auto c = text[i];
        if (c == '"' && (i == 0 || text[i - 1] != '\\'))
            quoted = !quoted;
        if (quoted)
            continue;
        if (c == '{')
        {
            if (depth++ == 0)
                object_begin = i + 1;
        }
        else if (c == '}' && depth > 0 && --depth == 0)
        {
            const auto object = text.substr(object_begin, i - object_begin);
            const auto name = text_string_value(object, "surfacePropertyName");
            if (!name)
                continue;
            game_surface_override value{};
            value.penetration = text_float_value(object, "bulletPenetrationDistanceModifier");
            value.damage = text_float_value(object, "bulletPenetrationDamageModifier");
            if (const auto material = text_string_value(object, "gamematerial"); material && !material->empty())
            {
                if (material->size() == 1)
                    value.material = static_cast<std::uint8_t>((*material)[0]);
                else if (std::uint16_t number{};
                         std::from_chars(material->data(), material->data() + material->size(), number).ec ==
                         std::errc{})
                {
                    value.material = number;
                }
            }
            result[lowercase(*name)] = value;
        }
    }
    return result;
}

[[nodiscard]] const surface_catalog_data& surface_catalog()
{
    static const auto catalog = []() -> surface_catalog_data
    {
        surface_catalog_data result{};

        // Without the catalog bullets fall back to built-in surface defaults: penetration is less exact but works.
        const auto unavailable = [&result](const std::string_view reason)
        {
            game::bound_memory().log.warning("[bvh] surface property catalog unavailable: {}", reason);
            return result;
        };
        const auto pak_path = assets::vpk_archive::locate_cs2_pak();
        if (pak_path.empty())
            return unavailable("pak01_dir.vpk not found");

        assets::vpk_archive pak{};
        if (!pak.open(pak_path))
            return unavailable("pak01_dir.vpk cannot be opened");
        const auto game_overrides = read_game_surface_overrides(pak);
        const auto default_surface = game_overrides.find("default");
        if (game_overrides.size() < 32 || default_surface == game_overrides.end() ||
            !default_surface->second.penetration || !default_surface->second.material)
            return unavailable("surfaceproperties_game.txt incomplete");

        const auto* entry = pak.find("surfaceproperties/surfaceproperties.vsurf_c");
        if (!entry)
        {
            return result;
        }

        assets::resource res{};
        if (!res.parse(pak.read(*entry)))
        {
            return result;
        }

        const auto* data = res.find("DATA");
        if (!data)
        {
            return result;
        }

        try
        {
            const auto doc = assets::kv3::decode(res.bytes(*data), data->size);
            if (const auto* list = doc.root.find("SurfacePropertiesList"); list && list->is_array())
            {
                struct node
                {
                    std::string name{};
                    std::string base{};
                    std::uint32_t hash{};
                    std::optional<float> density{};
                };
                std::vector<node> nodes{};
                nodes.reserve(list->size());
                std::unordered_map<std::string, std::size_t> name_index{};
                for (std::size_t i = 0; i < list->size(); ++i)
                {
                    const auto* item = list->at(i);
                    const auto* name = item ? item->find("surfacePropertyName") : nullptr;
                    const auto* base = item ? item->find("base") : nullptr;
                    const auto* hash = item ? item->find("m_nameHash") : nullptr;
                    const auto* physics = item ? item->find("physics") : nullptr;
                    const auto* density = physics ? physics->find("density") : nullptr;
                    nodes.push_back(
                        {name ? lowercase(name->as_string()) : std::string{},
                         base ? lowercase(base->as_string()) : std::string{},
                         static_cast<std::uint32_t>(hash ? hash->as_int() : 0),
                         density ? std::optional<float>{static_cast<float>(density->as_double())} : std::nullopt});
                    name_index.emplace(nodes.back().name, i);
                    result.hash_index.emplace(nodes.back().hash, static_cast<int>(i));
                }

                result.table.resize(nodes.size());
                result.densities.resize(nodes.size());
                for (std::size_t i = 0; i < nodes.size(); ++i)
                {
                    std::optional<float> penetration{};
                    std::optional<float> damage{};
                    std::optional<std::uint16_t> material{};
                    std::optional<float> density{};
                    auto current = i;
                    for (std::size_t guard = 0; guard < nodes.size(); ++guard)
                    {
                        if (const auto found = game_overrides.find(nodes[current].name); found != game_overrides.end())
                        {
                            if (!penetration && found->second.penetration)
                                penetration = found->second.penetration;
                            if (!damage && found->second.damage)
                                damage = found->second.damage;
                            if (!material && found->second.material)
                                material = found->second.material;
                        }
                        if (!density && nodes[current].density)
                            density = nodes[current].density;
                        if (nodes[current].base.empty())
                            break;
                        const auto parent = name_index.find(nodes[current].base);
                        if (parent == name_index.end() || parent->second == current)
                            break;
                        current = parent->second;
                    }
                    auto& surface = result.table[i];
                    result.densities[i] = density.value_or(0.0f);
                    surface.penetration_mod = penetration.value_or(*default_surface->second.penetration);
                    surface.unk_0C = damage.value_or(default_surface->second.damage.value_or(0.5f));
                    surface.surface_type = material.value_or(*default_surface->second.material);
                }
            }
        }
        catch (const std::exception& error)
        {
            // Bullets then use the built-in surface defaults; penetration is less exact but still works.
            game::bound_memory().log.warning("[bvh] surface property catalog unreadable: {}", error.what());
        }

        return result;
    }();
    return catalog;
}

void extract_phys(const assets::kv3::object& root, const surface_ctx& sctx, std::vector<collision_world::triangle>& out)
{
    std::uint64_t next_solid_id{1};
    std::vector<std::uint32_t> phys_ides{};
    if (const auto* h = root.find("m_surfacePropertyHashes"); h && h->is_array())
    {
        phys_ides.reserve(h->size());
        for (std::size_t i = 0; i < h->size(); ++i)
        {
            phys_ides.push_back(static_cast<std::uint32_t>(h->get(i).as_int()));
        }
    }

    std::vector<bool> excluded{};
    std::vector<std::uint32_t> contents{};
    std::vector<std::uint32_t> interacts_as{};
    if (const auto* attrs = root.find("m_collisionAttributes"); attrs && attrs->is_array())
    {
        excluded.reserve(attrs->size());
        for (std::size_t i = 0; i < attrs->size(); ++i)
        {
            excluded.push_back(attribute_is_excluded(attrs->at(i)));
            contents.push_back(attribute_contents(attrs->at(i)));
            interacts_as.push_back(attribute_interacts_as(attrs->at(i)));
        }
    }
    const auto is_excluded = [&](std::int64_t ci)
    { return ci >= 0 && static_cast<std::size_t>(ci) < excluded.size() && excluded[static_cast<std::size_t>(ci)]; };
    const auto contents_for = [&](std::int64_t ci)
    { return ci >= 0 && static_cast<std::size_t>(ci) < contents.size() ? contents[static_cast<std::size_t>(ci)] : 0u; };

    const auto interacts_for = [&](std::int64_t ci)
    {
        return ci >= 0 && static_cast<std::size_t>(ci) < interacts_as.size()
                   ? interacts_as[static_cast<std::size_t>(ci)]
                   : 0u;
    };

    const auto* parts = root.find("m_parts");
    if (!parts || !parts->is_array())
    {
        return;
    }

    for (std::size_t pi = 0; pi < parts->size(); ++pi)
    {
        const auto* shape = parts->get(pi).find("m_rnShape");
        if (!shape)
        {
            continue;
        }

        if (const auto* meshes = shape->find("m_meshes"); meshes && meshes->is_array())
        {
            for (std::size_t mi = 0; mi < meshes->size(); ++mi)
            {
                const auto* desc = meshes->at(mi);
                if (const auto* ci = desc->find("m_nCollisionAttributeIndex"); ci && is_excluded(ci->as_int()))
                {
                    continue;
                }
                const auto* si = desc->find("m_nSurfacePropertyIndex");
                auto surface = sctx.resolve(phys_ides, si ? si->as_int() : -1);
                if (const auto* ci = desc->find("m_nCollisionAttributeIndex"))
                {
                    surface.contents |= contents_for(ci->as_int());
                    surface.interacts_as |= interacts_for(ci->as_int());
                }
                append_mesh(desc->find("m_Mesh"), surface, sctx, phys_ides, next_solid_id++, out);
            }
        }

        if (const auto* hulls = shape->find("m_hulls"); hulls && hulls->is_array())
        {
            for (std::size_t hi = 0; hi < hulls->size(); ++hi)
            {
                const auto* desc = hulls->at(hi);
                if (const auto* ci = desc->find("m_nCollisionAttributeIndex"); ci && is_excluded(ci->as_int()))
                {
                    continue;
                }
                const auto* si = desc->find("m_nSurfacePropertyIndex");
                auto surface = sctx.resolve(phys_ides, si ? si->as_int() : -1);
                if (const auto* ci = desc->find("m_nCollisionAttributeIndex"))
                {
                    surface.contents |= contents_for(ci->as_int());
                    surface.interacts_as |= interacts_for(ci->as_int());
                }
                append_hull(desc->find("m_Hull"), surface, next_solid_id++, out);
            }
        }
    }
}

// Standard Source AngleMatrix (QAngle pitch/yaw/roll, degrees) + uniform
// scale, applied as world = origin + R * (scale * local).
struct entity_transform
{
    foundation::vec3 origin{};
    float scale{1.0f};
    float r[3][3]{};

    [[nodiscard]] foundation::vec3 apply(const foundation::vec3& l) const
    {
        const float x = l.x * scale, y = l.y * scale, z = l.z * scale;
        return {origin.x + r[0][0] * x + r[0][1] * y + r[0][2] * z, origin.y + r[1][0] * x + r[1][1] * y + r[1][2] * z,
                origin.z + r[2][0] * x + r[2][1] * y + r[2][2] * z};
    }
};

[[nodiscard]] entity_transform make_transform(const foundation::vec3& origin, const foundation::vec3& angles,
                                              float scale)
{
    constexpr float d2r = 3.14159265358979323846f / 180.0f;
    const float sp = std::sin(angles.x * d2r), cp = std::cos(angles.x * d2r);
    const float sy = std::sin(angles.y * d2r), cy = std::cos(angles.y * d2r);
    const float sr = std::sin(angles.z * d2r), cr = std::cos(angles.z * d2r);

    entity_transform t{};
    t.origin = origin;
    t.scale = (scale > 0.0f && std::isfinite(scale)) ? scale : 1.0f;
    t.r[0][0] = cp * cy;
    t.r[0][1] = sr * sp * cy - cr * sy;
    t.r[0][2] = cr * sp * cy + sr * sy;
    t.r[1][0] = cp * sy;
    t.r[1][1] = sr * sp * sy + cr * cy;
    t.r[1][2] = cr * sp * sy - sr * cy;
    t.r[2][0] = -sp;
    t.r[2][1] = sr * cp;
    t.r[2][2] = cr * cp;
    return t;
}

[[nodiscard]] std::uintptr_t entity_identity_token(std::uintptr_t entity)
{
    for (const auto offset : {std::uintptr_t{0x10}, std::uintptr_t{0x8}})
    {
        const auto identity = game::bound_memory().process.load<std::uintptr_t>(entity + offset);
        if (game::is_user_pointer(identity))
        {
            return identity;
        }
    }
    return 0;
}

std::mutex g_interactive_mutex;
std::shared_ptr<const std::vector<interactive_entity>> g_interactive{};

[[nodiscard]] bool is_solid_entity(const std::string& name)
{
    return name.find("FuncBrush") != std::string::npos || name.find("Prop") != std::string::npos ||
           name.find("Door") != std::string::npos || name.find("Breakable") != std::string::npos ||
           name.find("Shatter") != std::string::npos || name.find("Glass") != std::string::npos ||
           name.find("FuncMoveLinear") != std::string::npos;
}

[[nodiscard]] std::optional<bool> has_live_collision(std::uintptr_t entity, const std::string& class_name)
{
    const auto collision = entity + SCHEMA("C_BaseModelEntity", "m_Collision"_id);
    const auto flags_offset = static_cast<std::size_t>(SCHEMA("CCollisionProperty", "m_usSolidFlags"_id));
    const auto type_offset = static_cast<std::size_t>(SCHEMA("CCollisionProperty", "m_nSolidType"_id));
    std::uint16_t flags{};
    std::uint8_t type{};
    const auto begin = std::min(flags_offset, type_offset);
    const auto end = std::max(flags_offset + sizeof(flags), type_offset + sizeof(type));
    bool sampled{};
    if (end - begin <= 32)
    {
        std::array<std::uint8_t, 32> bytes{};
        sampled = game::bound_memory().process.copy(collision + begin, bytes.data(), end - begin);
        if (sampled)
        {
            std::memcpy(&flags, bytes.data() + flags_offset - begin, sizeof(flags));
            std::memcpy(&type, bytes.data() + type_offset - begin, sizeof(type));
        }
    }
    if (!sampled && (!game::bound_memory().process.copy(collision + flags_offset, &flags, sizeof(flags)) ||
                     !game::bound_memory().process.copy(collision + type_offset, &type, sizeof(type))))
    {
        return std::nullopt;
    }

    constexpr std::uint16_t fsolid_not_solid{0x0004};
    if (type == 0 || (flags & fsolid_not_solid) != 0)
        return false;

    if (class_name.find("DynamicProp") != std::string::npos)
    {
        bool create_non_solid{};
        if (!game::bound_memory().process.copy(entity + SCHEMA("C_DynamicProp", "m_bCreateNonSolid"_id),
                                               &create_non_solid, sizeof(create_non_solid)))
        {
            return std::nullopt;
        }
        if (create_non_solid)
            return false;
    }
    return true;
}

struct solid_cache_entry
{
    std::uintptr_t identity{};
    bool solid{};
    std::string class_name{};
    std::string model_path{};
};

// Archives and caches behind the entity geometry scan. pak01 has 132k entries, so it is opened once and kept; the
// map archive and the model and entity caches follow the current map. Used by the map parse worker only.
struct entity_geometry_sources
{
    assets::vpk_archive pak{};
    bool pak_tried{};
    std::string map{};
    assets::vpk_archive map_vpk{};
    std::unordered_map<std::string, std::vector<collision_world::triangle>> model_cache{};
    std::unordered_map<std::uintptr_t, solid_cache_entry> solid_entities{};
};

bool append_entity_geometry(entity_geometry_sources& sources, std::vector<collision_world::triangle>& out,
                            const std::string& map_name, const surface_ctx& sctx, std::uint64_t known_state_hash,
                            std::uint64_t& state_hash)
{
    std::optional<diagnostics::performance::scope> scan_profile;
    scan_profile.emplace(diagnostics::performance::zone::map_entities_scan);
    entity_scan_metrics metrics;
    auto& pak = sources.pak;
    if (!sources.pak_tried)
    {
        sources.pak_tried = true;
        if (const auto p = assets::vpk_archive::locate_cs2_pak(); !p.empty())
        {
            pak.open(p);
        }
    }
    const bool have_pak = pak.is_open();

    auto& map_vpk = sources.map_vpk;
    auto& model_cache = sources.model_cache;
    auto& solid_entity_cache = sources.solid_entities;
    if (sources.map != map_name)
    {
        sources.map = map_name;
        map_vpk = {};
        model_cache.clear();
        solid_entity_cache.clear();
        if (const auto mp = assets::vpk_archive::locate_map_vpk(map_name); !mp.empty())
        {
            map_vpk.open(mp);
        }
    }

    const auto model_local = [&](const std::string& model) -> const std::vector<collision_world::triangle>*
    {
        if (const auto it = model_cache.find(model); it != model_cache.end())
        {
            return &it->second;
        }

        std::vector<collision_world::triangle> local{};

        const assets::vpk_archive::entry* entry = have_pak ? pak.find(model) : nullptr;
        assets::vpk_archive* source = &pak;
        if (!entry)
        {
            entry = map_vpk.find(model);
            source = &map_vpk;
        }

        if (entry)
        {
            assets::resource res{};
            if (res.parse(source->read(*entry)))
            {
                if (const auto* phys = res.find("PHYS"))
                {
                    try
                    {
                        const auto doc = assets::kv3::decode(res.bytes(*phys), phys->size);
                        extract_phys(doc.root, sctx, local);
                    }
                    catch (const std::exception& error)
                    {
                        // The entity then has no collision of its own; the rest of the map is unaffected.
                        game::bound_memory().log.warning("[bvh] physics of {} unreadable: {}", model, error.what());
                    }
                }
            }
        }
        return &model_cache.emplace(model, std::move(local)).first->second;
    };

    const auto entity_list = game::bound_memory().process.load<std::uintptr_t>(game::bound_addresses().entity_list);
    if (!entity_list)
    {
        return false;
    }
    const auto scene_off = SCHEMA("C_BaseEntity", "m_pGameSceneNode"_id);

    struct entity_instance
    {
        std::uintptr_t identity{};
        std::string model{};
        foundation::vec3 origin{};
        foundation::vec3 angles{};
        float scale{};
    };
    std::vector<entity_instance> instances{};
    instances.reserve(256);
    std::vector<interactive_entity> interactive{};
    bool scan_complete = true;
    constexpr std::size_t k_entries_per_chunk{512};
    constexpr std::size_t k_entry_stride{0x70};
    constexpr std::size_t k_chunk_size{k_entries_per_chunk * k_entry_stride};
    std::array<std::uint8_t, k_chunk_size> chunk_buffer{};
    std::uintptr_t chunks[64]{};
    if (!game::bound_memory().process.copy(entity_list + 0x10, chunks, sizeof(chunks)))
    {
        return false;
    }

    for (int chunk_index = 0; chunk_index < 64; ++chunk_index)
    {
        const auto chunk = chunks[chunk_index];
        if (!chunk)
        {
            continue;
        }
        if (!game::bound_memory().process.copy(chunk, chunk_buffer.data(), chunk_buffer.size()))
        {
            scan_complete = false;
            continue;
        }

        metrics.slots += 512;
        for (int slot = 0; slot < 512; ++slot)
        {
            std::uintptr_t entity{};
            std::memcpy(&entity, chunk_buffer.data() + static_cast<std::size_t>(slot) * k_entry_stride, sizeof(entity));
            if (!game::is_user_pointer(entity))
            {
                continue;
            }

            ++metrics.seen;
            const auto identity = entity_identity_token(entity);
            auto cached_solid = solid_entity_cache.find(entity);
            if (cached_solid == solid_entity_cache.end() || cached_solid->second.identity != identity)
            {
                const auto class_name = game::entity_index().class_name(entity);
                if (class_name.empty())
                {
                    continue;
                }
                const auto solid = is_solid_entity(class_name);
                cached_solid =
                    solid_entity_cache.insert_or_assign(entity, solid_cache_entry{identity, solid, class_name, {}})
                        .first;
            }
            const auto solid = cached_solid->second.solid;

            if (!solid)
            {
                continue;
            }
            ++metrics.solids;
            const auto live_collision = has_live_collision(entity, cached_solid->second.class_name);
            if (!live_collision)
            {
                scan_complete = false;
                continue;
            }
            if (!*live_collision)
                continue;

            std::uintptr_t node{};
            if (!game::bound_memory().process.copy(entity + scene_off, &node, sizeof(node)))
            {
                scan_complete = false;
                continue;
            }
            if (!node)
            {
                continue;
            }

            // Doors and breakables for the walk bot, whether or not they have a model of their own (breakable glass,
            // boards and vents are mostly brushes without one, and would be skipped just below).
            {
                const auto& name = cached_solid->second.class_name;
                const bool door = name.find("Door") != std::string::npos;
                // Props break when they have health (boards, vents, crates); plain props have none.
                const bool breakable_prop =
                    name.find("Prop") != std::string::npos && game::bound_memory().process.load<std::int32_t>(
                                                                  entity + SCHEMA("C_BaseEntity", "m_iHealth"_id)) > 0;
                if (door || breakable_prop || name.find("Breakable") != std::string::npos ||
                    name.find("Glass") != std::string::npos || name.find("Shatter") != std::string::npos)
                {
                    std::array<std::byte, 28> placement{};
                    if (game::bound_memory().process.copy(node + 184, placement.data(), placement.size()))
                    {
                        interactive_entity found{.entity = entity, .door = door};
                        std::memcpy(&found.angles, placement.data(), sizeof(found.angles));
                        std::memcpy(&found.origin, placement.data() + 16, sizeof(found.origin));
                        interactive.push_back(found);
                    }
                }
            }
            auto model = cached_solid->second.model_path;
            if (model.empty())
            {
                model = game::read_model_path(game::bound_memory().process, node);
                if (!model.empty())
                    cached_solid->second.model_path = model;
            }
            if (model.empty())
            {
                continue;
            }

            entity_instance instance{.identity = identity, .model = model};
            std::array<std::byte, 28> transform{};
            if (!game::bound_memory().process.copy(node + 184, transform.data(), transform.size()))
            {
                scan_complete = false;
                continue;
            }
            std::memcpy(&instance.angles, transform.data(), sizeof(instance.angles));
            std::memcpy(&instance.scale, transform.data() + 12, sizeof(instance.scale));
            std::memcpy(&instance.origin, transform.data() + 16, sizeof(instance.origin));
            instances.push_back(std::move(instance));
            ++metrics.instances;
        }
    }

    if (!scan_complete)
    {
        metrics.incomplete = 1;
        return false;
    }
    {
        auto published = std::make_shared<const std::vector<interactive_entity>>(std::move(interactive));
        std::scoped_lock lock(g_interactive_mutex);
        g_interactive = std::move(published);
    }

    state_hash = 1469598103934665603ull;
    const auto mix = [&](std::uint32_t value)
    {
        state_hash ^= value;
        state_hash *= 1099511628211ull;
    };
    for (const auto& instance : instances)
    {
        mix(static_cast<std::uint32_t>(instance.identity));
        mix(static_cast<std::uint32_t>(instance.identity >> 32));
        for (const auto c : instance.model)
            mix(static_cast<std::uint8_t>(c));
        for (const auto& value : {instance.origin.x, instance.origin.y, instance.origin.z, instance.angles.x,
                                  instance.angles.y, instance.angles.z, instance.scale})
        {
            mix(std::bit_cast<std::uint32_t>(value));
        }
    }
    if (known_state_hash != 0 && state_hash == known_state_hash)
        return false;

    diagnostics::performance::record_counter(diagnostics::performance::counter::map_entity_changes);
    scan_profile.reset();
    VESTA_PERF_SCOPE(map_entities_build);
    for (const auto& instance : instances)
    {
        const auto* local = model_local(instance.model);
        if (!local || local->empty())
            continue;
        const auto xf = make_transform(instance.origin, instance.angles, instance.scale);
        out.reserve(out.size() + local->size());
        for (const auto& triangle : *local)
        {
            auto solid_id = instance.identity;
            solid_id ^= triangle.solid_id + 0x9e3779b97f4a7c15ull + (solid_id << 6) + (solid_id >> 2);
            out.push_back({.v0 = xf.apply(triangle.v0),
                           .v1 = xf.apply(triangle.v1),
                           .v2 = xf.apply(triangle.v2),
                           .surface = triangle.surface,
                           .solid_id = solid_id});
        }
    }
    return true;
}

} // namespace

// Archives and model geometry kept between entity refreshes of the current map. Map loader thread only.
struct collision_world::entity_sources : entity_geometry_sources
{
};

bool collision_world::build_from_map_file(const std::string& map_name_raw, const std::stop_token stop)
{
    m_file_failure = file_failure::none;
    if (stop.stop_requested())
    {
        m_file_failure = file_failure::cancelled;
        return false;
    }
    const auto map = base_map_name(map_name_raw);
    if (map.empty())
    {
        m_file_failure = file_failure::no_map_name;
        return false;
    }

    const auto vpk_path = assets::vpk_archive::locate_map_vpk(map);
    if (vpk_path.empty())
    {
        game::bound_memory().log.warning("[bvh] map vpk not found for \"{}\"", map);
        m_file_failure = file_failure::archive_not_found;
        return false;
    }

    assets::vpk_archive vpk{};
    if (!vpk.open(vpk_path))
    {
        game::bound_memory().log.warning("[bvh] failed to open map vpk {}", vpk_path);
        m_file_failure = file_failure::archive_unreadable;
        return false;
    }

    const auto* entry = vpk.find("maps/" + map + "/world_physics.vmdl_c");
    if (!entry)
    {
        game::bound_memory().log.warning("[bvh] world_physics.vmdl_c missing in {}", vpk_path);
        m_file_failure = file_failure::no_world_physics;
        return false;
    }

    if (stop.stop_requested())
    {
        m_file_failure = file_failure::cancelled;
        return false;
    }

    assets::resource resource{};
    if (!resource.parse(vpk.read(*entry)))
    {
        m_file_failure = file_failure::resource_invalid;
        return false;
    }

    const auto* phys = resource.find("PHYS");
    if (!phys)
    {
        game::bound_memory().log.warning("[bvh] no PHYS block in world_physics for {}", map);
        m_file_failure = file_failure::no_phys_block;
        return false;
    }

    assets::kv3::document doc{};
    try
    {
        doc = assets::kv3::decode(resource.bytes(*phys), phys->size);
    }
    catch (const assets::kv3::decode_error& e)
    {
        // The code (reason, KV3 version, compression) is the only part of the failure that reaches a release journal.
        diagnostics::emit(diagnostics::event::map_geometry_kv3_failed, diagnostics::severity::warning,
                          diagnostics::code_domain::mode, e.code());
        game::bound_memory().log.warning("[bvh] PHYS kv3 decode failed for {}: {}", map, e.what());
        m_file_failure = file_failure::kv3_failed;
        return false;
    }
    catch (const std::exception& e)
    {
        game::bound_memory().log.warning("[bvh] PHYS kv3 decode failed for {}: {}", map, e.what());
        m_file_failure = file_failure::kv3_failed;
        return false;
    }

    if (stop.stop_requested())
    {
        m_file_failure = file_failure::cancelled;
        return false;
    }

    // Both hash->handle and bullet material fields come from the VPK catalog.
    // This keeps static map collision independent of client.dll signatures.
    const auto& catalog = surface_catalog();
    const surface_ctx sctx{&catalog.hash_index, &catalog.table, &catalog.densities};
    if (catalog.hash_index.empty() || catalog.table.empty())
    {
        game::bound_memory().log.warning("[bvh] offline surface catalog unavailable -- penetration is fail-closed");
    }

    std::vector<triangle> fresh{};
    fresh.reserve(1u << 14);

    extract_phys(doc.root, sctx, fresh);
    if (stop.stop_requested())
    {
        m_file_failure = file_failure::cancelled;
        return false;
    }

    if (fresh.empty())
    {
        game::bound_memory().log.warning("[bvh] PHYS decoded but produced no world triangles for {}", map);
        m_file_failure = file_failure::no_triangles;
        return false;
    }

    const auto world_tris = fresh.size();
    diagnostics::emit(diagnostics::event::map_geometry_world, diagnostics::severity::information,
                      diagnostics::code_domain::mode, static_cast<std::uint32_t>(world_tris));

    // Publish immutable world collision first. Dynamic entity geometry is built by
    // refresh_map_entities after map_loader commits this world, so a heavy map
    // cannot hold the combat path hostage to entity extraction.
    game::bound_memory().log.info("[bvh] map geometry for {}: {} world triangles", map, world_tris);

    collision_world built{};
    built.m_triangles = std::move(fresh);
    built.m_world_triangle_count = world_tris;
    built.rebuild_accel();
    if (stop.stop_requested())
    {
        m_file_failure = file_failure::cancelled;
        return false;
    }
    {
        std::unique_lock lock(m_mutex);
        m_world_triangle_count = world_tris;
        m_map_name = map;
        m_entity_triangle_count = 0;
        m_triangles = std::move(built.m_triangles);
        m_nodes = std::move(built.m_nodes);
        m_indices = std::move(built.m_indices);
        m_tri_bounds = std::move(built.m_tri_bounds);
        m_centroids = std::move(built.m_centroids);
        m_world_render = std::move(built.m_world_render);
        m_entity_render.reset();
        m_entity_collision.reset();
        m_entity_state_hash = 0;
        ++m_world_render_revision;
        ++m_entity_render_revision;
        m_geometry_revision.fetch_add(1, std::memory_order_release);
    }
    return true;
}

void collision_world::refresh_map_entities(std::stop_token stop)
{
    VESTA_PERF_SCOPE(map_entities_refresh);
    if (stop.stop_requested())
        return;
    std::string map{};
    std::uint64_t previous_hash{};
    {
        std::shared_lock lock(m_mutex);
        if (m_world_triangle_count == 0 || m_world_triangle_count != m_triangles.size())
        {
            return; // no file build yet
        }
        map = m_map_name;
        previous_hash = m_entity_state_hash;
    }

    const auto& catalog = surface_catalog();
    const surface_ctx sctx{&catalog.hash_index, &catalog.table, &catalog.densities};

    std::vector<triangle> entity_geometry{};
    std::uint64_t entity_hash{};
    if (!m_entity_sources)
        m_entity_sources = std::make_shared<entity_sources>();
    if (!append_entity_geometry(*m_entity_sources, entity_geometry, map, sctx, previous_hash, entity_hash))
    {
        return;
    }

    auto built = std::make_shared<collision_world>();
    {
        VESTA_PERF_SCOPE(map_entities_build);
        built->m_triangles = std::move(entity_geometry);
        built->m_world_triangle_count = built->m_triangles.size();
        built->rebuild_accel();
    }

    if (stop.stop_requested())
        return;
    auto retired_render = built->m_world_render;
    std::shared_ptr<const collision_world> retired_collision = built->m_triangles.empty() ? nullptr : built;
    std::size_t total_triangles{};
    std::size_t previous_entity_triangles{};
    {
        VESTA_PERF_SCOPE(map_entities_publish);
        std::unique_lock lock(m_mutex);
        if (m_map_name != map || stop.stop_requested())
            return;
        total_triangles = m_triangles.size() + built->m_triangles.size();
        previous_entity_triangles = m_entity_triangle_count;
        m_entity_triangle_count = built->m_triangles.size();
        // The previous meshes retire after unlocking, including their large buffers.
        m_entity_render.swap(retired_render);
        m_entity_collision.swap(retired_collision);
        m_entity_state_hash = entity_hash;
        ++m_entity_render_revision;
        m_geometry_revision.fetch_add(1, std::memory_order_release);
    }
    // Moving doors and props republish every few hundred milliseconds; the journal only needs the size changing.
    if (built->m_triangles.size() != previous_entity_triangles)
        diagnostics::emit(diagnostics::event::map_geometry_entities, diagnostics::severity::information,
                          diagnostics::code_domain::mode, static_cast<std::uint32_t>(built->m_triangles.size()));
    diagnostics::performance::record_counter(diagnostics::performance::counter::map_geometry_published);
    game::bound_memory().log.info("[bvh] entity refresh for {}: {} entity triangles ({} total)", map,
                                  built->m_triangles.size(), total_triangles);
}

} // namespace simulation

namespace simulation
{
std::shared_ptr<const std::vector<interactive_entity>> interactive_entities()
{
    std::scoped_lock lock(g_interactive_mutex);
    return g_interactive;
}
} // namespace simulation
