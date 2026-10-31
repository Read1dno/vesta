#include <stdafx.hpp>
#include <game/memory/bound_services.hpp>
#include <game/body/body.hpp>
#include <foundation/color.hpp>

namespace game
{

namespace
{
struct alignas(16) transform_record
{
    foundation::vec3 translation{};
    float scale{};
    foundation::rotation orientation{};
};
static_assert(sizeof(transform_record) == 32);
} // namespace

bool skeleton_reader::data::is_valid() const
{
    const auto& origin = bones.front().position;
    return std::isfinite(origin.x) && std::isfinite(origin.y) && std::isfinite(origin.z) && origin.length_sqr() > 0.0f;
}

foundation::vec3 skeleton_reader::data::get_position(std::uint32_t id) const
{
    return id < bones.size() ? bones[id].position : foundation::vec3{};
}

foundation::rotation skeleton_reader::data::get_rotation(std::uint32_t id) const
{
    return id < bones.size() ? bones[id].rotation : foundation::rotation{};
}

float skeleton_reader::data::get_scale(std::uint32_t id) const
{
    return id < bones.size() ? bones[id].scale : 1.0f;
}

skeleton_reader::data skeleton_reader::get(std::uintptr_t bone_cache) const
{
    std::array<transform_record, 128> source{};
    static_assert(sizeof(source) == 4096);
    std::size_t bones_read = source.size();
    // One retry first: a read that fails once is usually a transient race, and accepting a partial skeleton for
    // it would put zero-position bones into aim and seed decisions.
    if (!game::bound_memory().process.copy(bone_cache, source.data(), sizeof(source)) &&
        !game::bound_memory().process.copy(bone_cache, source.data(), sizeof(source)))
    {
        // A custom model with few bones can end its cache before 128 transforms: read what exists, 16 bones
        // at a time, instead of dropping the player. At least the root bone is required, and `count` tells
        // consumers which bones are real.
        constexpr std::size_t step = 16;
        std::size_t read{};
        for (; read < source.size(); read += step)
            if (!game::bound_memory().process.copy(bone_cache + read * sizeof(transform_record), source.data() + read,
                                                   step * sizeof(transform_record)))
                break;
        if (read == 0)
            return {};
        std::fill(source.begin() + static_cast<std::ptrdiff_t>(read), source.end(), transform_record{});
        bones_read = read;
    }

    data snapshot{};
    // The scale is 1 for every bone except under the big head mutator (head_0 and the eyes). A slot that is not a
    // real transform (past the model's bones, a cache being rebuilt) reads as 1, never 0 or a huge value that
    // would collapse or blow up hitboxes.
    std::transform(source.begin(), source.end(), snapshot.bones.begin(),
                   [](const transform_record& value)
                   {
                       const auto scale = std::isfinite(value.scale) && value.scale >= 0.1f && value.scale <= 10.0f
                                              ? value.scale
                                              : 1.0f;
                       return data::bone{value.translation, value.orientation, scale};
                   });
    snapshot.count = static_cast<std::uint16_t>(bones_read);
    return snapshot;
}

} // namespace game
