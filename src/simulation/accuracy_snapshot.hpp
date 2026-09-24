#pragma once
#include <simulation/ballistics.hpp>
#include <simulation/inaccuracy_model.hpp>

namespace simulation::detail {
        inline float accuracy_from_snapshot(ballistics_t::inaccuracy_debug_data& dbg)
        {
            const auto mode=[&](const std::pair<float,float>& pair){return dbg.fire_mode==1?pair.second:pair.first;};
            shot_model::accuracy_input input{};
            input.velocity=dbg.velocity;input.view_angles=dbg.eye_angles;
            input.max_speed=mode(dbg.max_speed);input.move=mode(dbg.inaccuracy_move);
            input.penalty=dbg.accuracy_penalty;input.turning=dbg.turning_inaccuracy;
            input.jump_initial=dbg.inaccuracy_jump_initial;input.jump_apex=dbg.inaccuracy_jump_apex;
            input.walking=dbg.is_walking;input.grounded=dbg.on_ground;
            dbg.move_factor=dbg.move_inaccuracy=dbg.air_inaccuracy=dbg.strafing_inaccuracy=0;
            auto& vars=game::variables();
            if(!vars.try_get(CONVAR("weapon_accuracy_forcespread"_id),input.forcespread)
                || !std::isfinite(input.forcespread))
                return std::numeric_limits<float>::quiet_NaN();
            if(input.forcespread>0) return dbg.final_inaccuracy=std::min(input.forcespread,1.0f);
            if(!vars.try_get(CONVAR("weapon_accuracy_nospread"_id),input.nospread))
                return std::numeric_limits<float>::quiet_NaN();
            if(input.nospread) return dbg.final_inaccuracy=0;
            if(!vars.try_get(CONVAR("sv_strafing_inaccuracy_enabled"_id),input.strafing))
                return std::numeric_limits<float>::quiet_NaN();
            if(!input.grounded && (!vars.try_get(CONVAR("weapon_air_spread_scale"_id),input.air_scale)
                || !vars.try_get(CONVAR("sv_jump_impulse"_id),input.jump_impulse)))
                return std::numeric_limits<float>::quiet_NaN();
            if(input.strafing && (!vars.try_get(CONVAR("sv_strafing_inaccuracy_bias"_id),input.strafe_bias)
                || !vars.try_get(CONVAR("sv_strafing_inaccuracy_scale"_id),input.strafe_scale)))
                return std::numeric_limits<float>::quiet_NaN();
            shot_model::accuracy_result result{};
            if(!shot_model::accuracy(input,result)) return std::numeric_limits<float>::quiet_NaN();
            dbg.move_factor=result.movement_factor;dbg.move_inaccuracy=result.movement;
            dbg.air_inaccuracy=result.air;dbg.strafing_inaccuracy=result.strafe;
            dbg.final_inaccuracy=result.total;
            return result.total;
        }

}
