#pragma once

#include <simulation/shot_model.hpp>

namespace simulation::shot_model {
struct recoil_state {
    shot_time base{};
    vec3 angle{}, velocity{};
};
class recoil_cache {
public:
    bool sample(const recoil_state& state, shot_time target, vec3& output) noexcept {
        if (!finite(state.angle) || !finite(state.velocity) || !std::isfinite(state.base.fraction)
            || !std::isfinite(target.fraction) || target.fraction < 0 || target.fraction >= 1
            || state.base.fraction < 0 || state.base.fraction >= 1) return false;
        if (!m_count || !same(state)) {
            m_state=state; m_values[0]=state.angle; m_count=1;
        }
        const auto delta=static_cast<std::int64_t>(target.tick)-state.base.tick;
        const float elapsed=std::max(0.0f, static_cast<float>(delta)
            + target.fraction-state.base.fraction) * 0.015625f;
        const float position=elapsed*128.0f;
        if (!std::isfinite(position) || position>10000000.0f) return false;
        const int index=static_cast<int>(position);
        const int needed=std::min(index+1,128);
        constexpr float decay=0.9394130628f; // exp(-0.0625), native 1/128 s step.
        while (m_count<=needed) {
            const int n=m_count;
            vec3 value=m_values[n-1]*decay;
            const float magnitude=length(value);
            value=magnitude<=0.140625f ? vec3{} : value*(1.0f-0.140625f/magnitude);
            for (int endpoint : {n-1,n}) {
                auto velocity=state.velocity*std::exp(-4.5f*(float(endpoint)/128.0f));
                if (length(velocity)<0.03125f) velocity={};
                value+=velocity*0.00390625f;
            }
            m_values[m_count++]=value;
        }
        const auto at=[&](int n) {
            auto value=n<m_count ? m_values[n]
                : m_values[m_count-1]*std::pow(decay,float(n-m_count+1));
            return vec3{std::clamp(value.x,-89.0f,89.0f),
                std::clamp(value.y,-89.0f,89.0f),std::clamp(value.z,-89.0f,89.0f)};
        };
        auto a=quaternion(at(index));
        auto b=quaternion(at(index+1));
        const float dot=(a.x*b.x+a.y*b.y)+(a.z*b.z+a.w*b.w);
        const float d=std::abs(dot), t=position-float(index), h=t-0.5f;
        float weight=((((3.55645f-d*1.43519f)*d-3.2452f)*d+1.0904f)*h*h
            +(d*0.215638f-1.06021f)*d+0.848013f)*t*h*(t-1.0f)+t;
        const float inverse=1.0f-weight;
        if (dot<=0) weight=-weight;
        foundation::rotation q{inverse*a.x+weight*b.x,inverse*a.y+weight*b.y,
            inverse*a.z+weight*b.z,inverse*a.w+weight*b.w};
        const float norm=std::sqrt((q.x*q.x+q.y*q.y)+(q.z*q.z+q.w*q.w));
        if (!(norm>0) || !std::isfinite(norm)) return false;
        q.x/=norm;q.y/=norm;q.z/=norm;q.w/=norm;
        const float fx=1.0f-2.0f*q.y*q.y-2.0f*q.z*q.z;
        const float fy=2.0f*(q.x*q.y+q.z*q.w);
        const float fz=2.0f*(q.x*q.z-q.y*q.w);
        const float left_z=2.0f*(q.y*q.z+q.x*q.w);
        const float up_z=1.0f-2.0f*q.x*q.x-2.0f*q.y*q.y;
        output={foundation::to_degrees(std::atan2(-fz,std::sqrt(fx*fx+fy*fy))),
            foundation::to_degrees(std::atan2(fy,fx)),
            foundation::to_degrees(std::atan2(left_z,up_z))};
        if (length(output)<0.03125f) output={};
        output*=2.0f;
        return finite(output);
    }
private:
    static foundation::rotation quaternion(const vec3& value) noexcept {
        constexpr float factor=0.00872664625997f;
        const float sp=std::sin(value.x*factor), cp=std::cos(value.x*factor);
        const float sy=std::sin(value.y*factor), cy=std::cos(value.y*factor);
        const float sr=std::sin(value.z*factor), cr=std::cos(value.z*factor);
        return {sr*cp*cy-cr*sp*sy, cr*sp*cy+sr*cp*sy,
            cr*cp*sy-sr*sp*cy,cr*cp*cy+sr*sp*sy};
    }
    bool same(const recoil_state& s) const noexcept {
        return s.base.tick==m_state.base.tick && s.base.fraction==m_state.base.fraction
            && s.angle.x==m_state.angle.x && s.angle.y==m_state.angle.y && s.angle.z==m_state.angle.z
            && s.velocity.x==m_state.velocity.x && s.velocity.y==m_state.velocity.y
            && s.velocity.z==m_state.velocity.z;
    }
    recoil_state m_state{};
    std::array<vec3,129> m_values{};
    int m_count{};
};
} // namespace simulation::shot_model
