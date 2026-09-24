#include <stdafx.hpp>
#include <features/trigger/seed_state.hpp>
#include <simulation/shot_model.hpp>
#include <simulation/punch_snapshot.hpp>

namespace features::aimbot {
float seed_quantize_angle(float angle)
{
    return simulation::shot_model::quantize(angle);
}

std::optional<foundation::vec3> read_seed_punch(std::uintptr_t pawn)
{
    return simulation::read_cached_punch(app::context().process,pawn,
        SCHEMA("C_CSPlayerPawn", "m_pAimPunchServices"_id));
}
}
