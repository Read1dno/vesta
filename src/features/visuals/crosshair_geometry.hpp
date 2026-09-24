#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace features::visuals::detail {
struct crosshair_rect { float x0{},y0{},x1{},y1{}; };
struct crosshair_rects { std::array<crosshair_rect,5> pieces{}; std::size_t count{}; };
inline float scale_crosshair_parameter(float value, float height, float reference_height)
{
    if (!std::isfinite(value) || !std::isfinite(height)
        || !std::isfinite(reference_height) || height<=0 || reference_height<=0) return 0;
    return value>0 ? std::max(1.0f,std::round(value*height/reference_height)) : value;
}
inline crosshair_rects native_static_crosshair(float width,float height,float gap,float length,
    float thickness,bool dot,bool t_style,bool dot_only=false)
{
    crosshair_rects result{};
    for(float v:{width,height,gap,length,thickness}) if(!std::isfinite(v)) return result;
    if (width<=0 || height<=0 || thickness<=0 || thickness>512 || length>4096) return result;
    const auto center_x=std::floor(width*0.5f),center_y=std::floor(height*0.5f);
    const auto pixels=std::max(1,static_cast<int>(std::round(thickness)));
    const auto lower=static_cast<float>((pixels+1)/2),upper=static_cast<float>(pixels)-lower;
    const auto adjustment=lower==upper ? 0.0f : -1.0f;
    const auto push=[&](float x0,float y0,float x1,float y1) {
        if(x1>x0 && y1>y0) result.pieces[result.count++]={x0,y0,x1,y1};
    };
    if(dot || dot_only) push(center_x-lower,center_y-lower,center_x+upper,center_y+upper);
    if(!dot_only && length>0) {
        const auto space=std::max(1.0f,gap);
        const auto left=std::floor(center_x-space),right=std::ceil(center_x+space)+adjustment;
        push(left-length,center_y-lower,left,center_y+upper);
        push(right,center_y-lower,right+length,center_y+upper);
        if(!t_style) {
            const auto top=std::floor(center_y-space-length);
            push(center_x-lower,top,center_x+upper,top+length);
        }
        const auto bottom=std::ceil(center_y+space)+adjustment;
        push(center_x-lower,bottom,center_x+upper,bottom+length);
    }
    return result;
}
} // namespace features::visuals::detail
