#pragma once

#include <features/chams/mesh.hpp>
#include <assets/vpk.hpp>
#include <foundation/math/vector.hpp>

#include <string>
#include <vector>

namespace features::chams
{

[[nodiscard]] skinned_mesh extract_mesh(assets::vpk_archive& vpk, const std::string& model_path);

// One hitbox of a model file: a capsule between `mins` and `maxs` in its bone's space. `bone` indexes the mesh's
// skeleton (skinned_mesh::bones), `index` is the hitbox index the game uses (0 head, 1 neck, ...).
struct model_hitbox
{
    int index{-1};
    int bone{-1};
    std::string name{};
    foundation::vec3 mins{};
    foundation::vec3 maxs{};
    float radius{};
};

// The hitboxes of a model as authored in its file (the "cstrike" set of m_hitboxsets in the model data), with the
// bones resolved by name against `mesh`. Empty when the model has none or the file cannot be read.
[[nodiscard]] std::vector<model_hitbox> extract_hitboxes(assets::vpk_archive& vpk, const std::string& model_path,
                                                         const skinned_mesh& mesh);

} // namespace features::chams
