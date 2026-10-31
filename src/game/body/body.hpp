#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <shared_mutex>
#include <unordered_map>

#include <foundation/math/vector.hpp>

namespace game
{

class skeleton_reader
{
public:
    struct data
    {
        struct bone
        {
            foundation::vec3 position{};
            foundation::rotation rotation{};
            // Uniform scale of the bone's space, 1 normally. The big head mutator (casual) scales head_0 by the
            // player's m_flBigHeadScale, and the eyes under it inherit it: the head mesh and the head hitbox grow
            // or shrink around the bone, on the server as well.
            float scale{1.0f};

            // A point given in the bone's own space (a hitbox capsule end, a remembered aim offset), in the world.
            [[nodiscard]] foundation::vec3 to_world(const foundation::vec3& local) const noexcept
            {
                return position + rotation.apply(local * scale);
            }
            // A world point in the bone's own space: the inverse of to_world().
            [[nodiscard]] foundation::vec3 to_local(const foundation::vec3& world) const noexcept
            {
                const foundation::rotation inverse{-rotation.x, -rotation.y, -rotation.z, rotation.w};
                return inverse.apply(world - position) / scale;
            }
        };

        std::array<bone, 128> bones{};
        // How many bones were really read: 128 normally, fewer only for a model whose bone cache ends early.
        std::uint16_t count{128};

        [[nodiscard]] bool covers(int bone) const { return bone >= 0 && bone < count; }
        [[nodiscard]] bool is_valid() const;
        [[nodiscard]] foundation::vec3 get_position(std::uint32_t id) const;
        [[nodiscard]] foundation::rotation get_rotation(std::uint32_t id) const;
        [[nodiscard]] float get_scale(std::uint32_t id) const;
    };

    [[nodiscard]] data get(std::uintptr_t bone_array) const;
};

class bounds_projector
{
public:
    struct data
    {
        foundation::vec2 min{};
        foundation::vec2 max{};

        [[nodiscard]] bool is_valid() const;
        [[nodiscard]] float width() const { return max.x - min.x; }
        [[nodiscard]] float height() const { return max.y - min.y; }
    };

    [[nodiscard]] data get(const skeleton_reader::data& bone_data) const;
};

class hitbox_catalog
{
public:
    struct entry
    {
        int index{-1};
        int bone{-1};
        std::array<char, 16> name{};
        foundation::vec3 mins{};
        foundation::vec3 maxs{};
        float radius{};
    };

    struct set
    {
        std::array<entry, 20> entries{};
        int count{};

        [[nodiscard]] const entry* begin() const { return entries.data(); }
        [[nodiscard]] const entry* end() const { return entries.data() + count; }
    };

    // Hitbox geometry already read for scene nodes, reused while a node keeps the same model. Not thread-safe:
    // every reading thread owns its own cache, so the per-tick readers never contend for a lock.
    class geometry_cache
    {
    public:
        void clear() noexcept { m_nodes.clear(); }

    private:
        friend class hitbox_catalog;
        struct node_geometry
        {
            int handle_offset{};
            std::uintptr_t model_handle{};
            set geometry{};
        };
        std::unordered_map<std::uintptr_t, node_geometry> m_nodes{};
    };

    // Reads the hitboxes of a scene node; with a cache, a node whose model has not changed is served from it.
    [[nodiscard]] set query(std::uintptr_t game_scene_node, geometry_cache* cache = nullptr) const;
    [[nodiscard]] int hitgroup_from_hitbox(int hitbox) const;
    void remember(const set& snapshot);
    [[nodiscard]] set snapshot() const;

private:
    set m_snapshot{};
    mutable std::shared_mutex m_snapshot_mutex{};

    // Index of the pointer chain (scene node -> hitbox set) that last worked, -1 before the first success.
    mutable std::atomic<int> m_active_chain{-1};
};

// A hitbox capsule placed on its bone: both axis ends and the radius in world units. The bone's scale applies to
// all three (a big head's hitbox is longer and wider, not only moved).
struct world_capsule
{
    foundation::vec3 start{};
    foundation::vec3 end{};
    float radius{};

    [[nodiscard]] foundation::vec3 center() const noexcept { return (start + end) * 0.5f; }
};

[[nodiscard]] inline world_capsule place_hitbox(const skeleton_reader::data::bone& bone,
                                                const hitbox_catalog::entry& hitbox) noexcept
{
    return {bone.to_world(hitbox.mins), bone.to_world(hitbox.maxs), hitbox.radius * bone.scale};
}

} // namespace game
