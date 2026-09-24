#include <stdafx.hpp>
#include <features/visuals/crosshair_sync.hpp>
#include <features/visuals/crosshair_geometry.hpp>
#include <core/memory/compatibility.hpp>
#include <core/memory/compatibility_contracts.hpp>

namespace features::visuals {
namespace {
struct settings {
    int height{}, gap{}, length{}, thickness{}, style{}, r{},g{},b{},a{};
    bool dot{},t_style{},outline{},valid{};
};
settings read_settings()
{
    settings s{};
    auto& vars=game::variables();
    const auto get=[&](auto& value,std::uint32_t id) { return vars.try_get(vars.find(id),value); };
    s.valid=get(s.height,"cl_crosshair_screen_height"_id)
        && get(s.gap,"cl_crosshair_gap"_id) && get(s.length,"cl_crosshair_length"_id)
        && get(s.thickness,"cl_crosshair_thickness"_id) && get(s.style,"cl_crosshairstyle"_id)
        && get(s.r,"cl_crosshaircolor_r"_id) && get(s.g,"cl_crosshaircolor_g"_id)
        && get(s.b,"cl_crosshaircolor_b"_id) && get(s.a,"cl_crosshaircolor_a"_id)
        && get(s.dot,"cl_crosshairdot"_id) && get(s.t_style,"cl_crosshair_t"_id)
        && get(s.outline,"cl_crosshair_drawoutline"_id)
        && s.height>=240 && s.height<=16384 && s.style>=0 && s.style<=7
        && s.gap>=-256 && s.gap<=4096 && s.length>=0 && s.length<=4096
        && s.thickness>=0 && s.thickness<=512;
    return s;
}
zdraw::rgba rgba(const std::array<float,4>& linear)
{
    const auto channel=[](float v) {
        return static_cast<std::uint8_t>(std::lround(game::compatibility::detail::linear_to_srgb(v)*255.0f));
    };
    return {channel(linear[0]),channel(linear[1]),channel(linear[2]),
        static_cast<std::uint8_t>(std::lround(std::clamp(linear[3],0.0f,1.0f)*255.0f))};
}
}
void draw_game_crosshair(zdraw::draw_list& draw, float width, float height,
    const zdraw::rgba* override_color)
{
    if (!draw.m_im_draw_list) return;
    static settings cfg{};
    static auto refresh=std::chrono::steady_clock::time_point{};
    const auto now=std::chrono::steady_clock::now();
    if (now>=refresh) { cfg=read_settings(); refresh=now+std::chrono::milliseconds(33); }
    if (!cfg.valid) return;
    float native_width{},native_height{};
    if (!game::compatibility::crosshair_dimensions(native_width,native_height)) return;
    const float sx=width/native_width,sy=height/native_height;
    game::compatibility::crosshair_frame frame{};
    if (!game::compatibility::read_crosshair(frame)) return;
    if (frame.count) {
        auto* list=draw.m_im_draw_list;
        for (std::size_t i=0;i<frame.count;++i) {
            const auto& p=frame.pieces[i];
            auto color=rgba(p.color);
            const auto outline=rgba(p.outline);
            if (override_color) {
                const auto alpha=color.a; color=*override_color;
                color.a=static_cast<std::uint8_t>(unsigned(color.a)*alpha/255);
            }
            const auto fill=zdraw::draw_list::to_im_color(color);
            const auto border=zdraw::draw_list::to_im_color(outline);
            const auto& g=p.geometry;
            if(p.type==0) {
                // Native rectangles store inclusive last-pixel coordinates.
                if(outline.a) list->AddRectFilled({(g[0]-1)*sx,(g[1]-1)*sy},
                    {(g[2]+2)*sx,(g[3]+2)*sy},border);
                if(color.a) list->AddRectFilled({g[0]*sx,g[1]*sy},{(g[2]+1)*sx,(g[3]+1)*sy},fill);
            } else {
                const auto outer=std::max(g[2],0.0f),inner=std::max(0.0f,outer-g[3]);
                const auto start=p.type==1 ? -std::numbers::pi_v<float>
                    : p.rotation-std::numbers::pi_v<float>*0.5f-p.angle;
                const auto end=p.type==1 ? std::numbers::pi_v<float>
                    : p.rotation-std::numbers::pi_v<float>*0.5f+p.angle;
                if (end<=start || outer<=0) continue;
                const auto ring=[&](float r0,float r1,ImU32 packed) {
                    if (!(packed&IM_COL32_A_MASK)) return;
                    const int segments=std::clamp(static_cast<int>(std::ceil((end-start)*std::sqrt(outer))),16,256);
                    const auto uv=ImGui::GetFontTexUvWhitePixel();
                    const auto transparent=packed&~IM_COL32_A_MASK;
                    list->PrimReserve(segments*18,(segments+1)*4);
                    const auto base=list->_VtxCurrentIdx;
                    for(int k=0;k<=segments;++k) {
                        const auto angle=start+(end-start)*k/segments;
                        const auto c=std::cos(angle),s=std::sin(angle);
                        const auto nx=c/sx,ny=s/sy;
                        const auto norm=std::max(std::hypot(nx,ny),0.0001f);
                        const ImVec2 feather{nx/norm*0.75f,ny/norm*0.75f};
                        const ImVec2 outside{(g[0]+c*r1)*sx,(g[1]+s*r1)*sy};
                        const ImVec2 inside{(g[0]+c*r0)*sx,(g[1]+s*r0)*sy};
                        list->PrimWriteVtx(outside+feather,uv,transparent);
                        list->PrimWriteVtx(outside,uv,packed);
                        list->PrimWriteVtx(inside,uv,packed);
                        list->PrimWriteVtx(inside-feather,uv,transparent);
                    }
                    for(int k=0;k<segments;++k) for(int layer=0;layer<3;++layer) {
                        const auto a=base+k*4+layer,b=a+4;
                        for(auto index:{a,b,a+1,a+1,b,b+1})
                            list->PrimWriteIdx(static_cast<ImDrawIdx>(index));
                    }
                };
                if(outline.a) ring(std::max(0.0f,inner-1),outer+1,border);
                if(color.a) ring(inner,outer,fill);
            }
        }
        return;
    }

    // Weapons without a native reticle still receive the user's static geometry.
    const auto scaled=[&](int v) { return detail::scale_crosshair_parameter(float(v),native_height,float(cfg.height)); };
    const auto geometry=detail::native_static_crosshair(native_width,native_height,
        scaled(cfg.gap),scaled(cfg.length),scaled(cfg.thickness),cfg.dot,cfg.t_style,cfg.style==6);
    const auto byte=[](int v){return static_cast<std::uint8_t>(std::clamp(v,0,255));};
    auto color=zdraw::rgba{byte(cfg.r),byte(cfg.g),byte(cfg.b),byte(cfg.a)};
    if(override_color) { color=*override_color; color.a=static_cast<std::uint8_t>(unsigned(color.a)*byte(cfg.a)/255); }
    for(std::size_t i=0;i<geometry.count;++i) {
        const auto& p=geometry.pieces[i];
        if(cfg.outline) draw.add_rect_filled((p.x0-1)*sx,(p.y0-1)*sy,
            (p.x1-p.x0+2)*sx,(p.y1-p.y0+2)*sy,{0,0,0,color.a});
        draw.add_rect_filled(p.x0*sx,p.y0*sy,(p.x1-p.x0)*sx,(p.y1-p.y0)*sy,color);
    }
}
} // namespace features::visuals
