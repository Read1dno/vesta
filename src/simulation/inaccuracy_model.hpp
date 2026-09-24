#pragma once

#include <simulation/shot_model.hpp>

namespace simulation::shot_model {
struct accuracy_input {
    vec3 velocity{}, view_angles{};
    float max_speed{}, move{}, penalty{}, turning{};
    float jump_initial{}, jump_apex{}, air_scale{1}, jump_impulse{301.993377f};
    float forcespread{}, strafe_bias{0.5f}, strafe_scale{0.1f};
    bool walking{}, grounded{}, nospread{}, strafing{};
};
struct accuracy_result { float total{}, movement_factor{}, movement{}, air{}, strafe{}; };
inline bool accuracy(const accuracy_input& p, accuracy_result& out) noexcept {
    out={};
    if (!finite(p.velocity) || !finite(p.view_angles)) return false;
    for(float f : {p.max_speed,p.move,p.penalty,p.turning,p.jump_initial,p.jump_apex,
        p.air_scale,p.jump_impulse,p.forcespread,p.strafe_bias,p.strafe_scale})
        if (!std::isfinite(f)) return false;
    if (p.forcespread>0) {out.total=std::min(p.forcespread,1.0f);return true;}
    if (p.nospread) return true;
    if (p.max_speed<0 || p.move<0 || p.jump_initial<0 || p.air_scale<0 || p.jump_impulse<0) return false;
    const float speed=std::sqrt(p.velocity.x*p.velocity.x+p.velocity.y*p.velocity.y);
    const float low=p.max_speed*0.34f, high=p.max_speed*0.95f;
    out.movement_factor=low==high ? (speed>=high?1.0f:0.0f)
        : std::clamp((speed-low)/(high-low),0.0f,1.0f);
    if(out.movement_factor>0 && !p.walking) out.movement_factor=std::pow(out.movement_factor,0.25f);
    out.movement=out.movement_factor*p.move;
    if(!p.grounded) {
        const float initial=p.jump_initial*p.air_scale, apex=p.jump_apex*p.air_scale;
        const float impulse=std::sqrt(p.jump_impulse), current=std::sqrt(std::abs(p.velocity.z));
        const float lower=impulse*0.25f;
        out.air=lower==impulse ? (current>=impulse?initial:apex)
            : ((current-lower)*(initial-apex))/(impulse-lower)+apex;
        out.air=std::clamp(out.air,0.0f,initial*2.0f);
    }
    if(p.strafing && p.velocity.length_sqr()>=0.001f) {
        const float pitch=foundation::to_radians(p.view_angles.x), yaw=foundation::to_radians(p.view_angles.y);
        const vec3 forward{std::cos(pitch)*std::cos(yaw),std::cos(pitch)*std::sin(yaw),-std::sin(pitch)};
        const float value=std::clamp(1.0f-std::abs((p.velocity/length(p.velocity)).dot(forward)),0.0f,1.0f);
        const float bias=std::clamp(p.strafe_bias,std::numeric_limits<float>::min(),1.0f);
        const float shaped=value/((1.0f/bias-2.0f)*(1.0f-value)+1.0f);
        out.strafe=(length(p.velocity)/250.0f)*shaped*p.strafe_scale;
    }
    out.total=std::min(1.0f,out.strafe+(p.penalty+out.movement+out.air)+p.turning);
    return std::isfinite(out.total) && out.total>=0;
}
} // namespace simulation::shot_model
