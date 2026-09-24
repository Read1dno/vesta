#include <stdafx.hpp>
#include <core/memory/compatibility.hpp>
#include <core/memory/compatibility_contracts.hpp>
#include <fstream>
#include <features/visuals/visuals.hpp>

namespace game::compatibility {
namespace {
auto& process() { return app::context().process; }
bool pointer(std::uintptr_t p) { return p>=0x10000 && p<=0x00007fffffffffffULL; }
bool inside(std::uintptr_t p, std::size_t n=1)
{
    const auto base=app::context().modules.client;
    const auto size=process().module_image_size(base);
    return p>=base && p-base<=size && n<=size-(p-base);
}
std::uintptr_t unique(std::string_view name, std::string_view pattern)
{
    std::size_t count{};
    const auto address=process().scan_unique_code_signature(app::context().modules.client,pattern,count);
    if (!address) app::context().diagnostics.warning("[compat] {}: signature matches={}, feature unavailable",name,count);
    return address;
}
struct crosshair_addresses { std::uintptr_t sequence{}, buffers{}; };
const crosshair_addresses& crosshair_source()
{
    static const auto source=[] {
        crosshair_addresses result{};
        const auto code=unique("crosshair publication",
            "8B 05 ? ? ? ? 48 8D 4D ? FF C8 33 D2 83 E0 01 41 B8 10 04 00 00 "
            "48 69 D8 10 04 00 00 48 8D 05 ? ? ? ? 48 03 D8 E8");
        if (!code) return result;
        const auto sequence=process().decode_rip(code,2,6);
        const auto buffers=process().decode_rip(code+30);
        if (inside(sequence,4) && inside(buffers,2*sizeof(crosshair_frame)))
            result={sequence,buffers};
        return result;
    }();
    return source;
}
}
std::uint32_t shot_punch_offset()
{
    static const auto offset=[] {
        const auto code=unique("pre-shot punch",
            "F2 0F 10 4C 24 ? 8B 44 24 ? F2 0F 11 4F 2C 89 47 34 F2 0F 11 8E ? ? ? ? 89 86 ? ? ? ?");
        std::array<std::byte,32> bytes{};
        return code && process().copy(code,bytes.data(),bytes.size())
            ? detail::decode_shot_punch(bytes) : 0;
    }();
    return offset;
}
std::uintptr_t spread_patterns()
{
    static const auto address=[] {
        auto code=unique("spread map v2","48 8D 0D ? ? ? ? 45 03 CC E8 ? ? ? ? EB ? 41 0F 28 CA");
        if (!code) code=unique("spread map v1",
            "48 8D 0D ? ? ? ? 48 8D 44 24 30 0F B7 D6 48 89 44 24 20 45 0F AF CD 45 03 CC E8");
        const auto map=code ? process().decode_rip(code) : 0;
        return inside(map,32) ? map : 0;
    }();
    return address;
}
std::optional<radar_layout> radar()
{
    static const auto result=[]()->std::optional<radar_layout> {
        const auto code=unique("radar transform",
            "84 C0 74 0A F3 0F 10 81 ? ? ? ? EB 10 F3 0F 10 81 ? ? ? ? F3 0F 5E 81 ? ? ? ? "
            "F3 44 0F 10 B2 ? ? ? ? 0F 57 FF F3 44 0F 10 B9 ? ? ? ? F3 44 0F 5C B1 ? ? ? ?");
        std::array<std::byte,60> bytes{};
        if (!code || !process().copy(code,bytes.data(),bytes.size())) return {};
        return detail::decode_radar(bytes);
    }();
    return result;
}
panel_layout panel(std::uintptr_t ui)
{
    if (!pointer(ui)) return {};
    const auto table=process().load<std::uintptr_t>(ui);
    static thread_local std::unordered_map<std::uintptr_t,panel_layout> cache;
    if (const auto it=cache.find(table); it!=cache.end()) return it->second;
    panel_layout result{};
    const auto get=[&](std::uintptr_t slot) {
        std::array<std::byte,16> bytes{};
        const auto fn=process().load<std::uintptr_t>(table+slot);
        return pointer(fn) && process().copy(fn,bytes.data(),bytes.size())
            ? detail::float_getter(bytes) : 0;
    };
    result.width=get(0x320); result.height=get(0x328);
    result.x=get(0x398); result.y=get(0x3a0);
    std::array<std::byte,13> visible{};
    if (const auto fn=process().load<std::uintptr_t>(table+0xb8);
        pointer(fn) && process().copy(fn,visible.data(),visible.size())
        && visible[0]==std::byte{0x0f} && visible[1]==std::byte{0xb6}
        && visible[2]==std::byte{0x81} && visible[7]==std::byte{0xc0}
        && visible[8]==std::byte{0xe8} && visible[9]==std::byte{3}
        && visible[12]==std::byte{0xc3})
        result.visible=detail::field<std::uint32_t>(visible,3);
    if (result.height!=result.width+4 || result.y!=result.x+4 || result.visible<0x100 || result.visible>0x800)
        result={};
    if (cache.size()>16) cache.clear();
    cache.emplace(table,result);
    return result;
}
std::uintptr_t hud_element(std::string_view wanted)
{
    static const auto global=[] {
        const auto fn=unique("HUD directory",
            "40 53 48 83 EC 20 48 8B 05 ? ? ? ? 48 8B D9 48 85 C0 74 ? 48 89 5C 24 ? 48 8D 88 58 02 00 00");
        return fn ? process().decode_rip(fn+6) : 0;
    }();
    if (!global) return 0;
    const auto root=process().load<std::uintptr_t>(global);
    if (!pointer(root)) return 0;
    std::array<std::byte,32> header{};
    if (!process().copy(root+0x258,header.data(),header.size())) return 0;
    const auto capacity=detail::field<std::uint32_t>(header,12)&0x7fffffff;
    const auto data=detail::field<std::uintptr_t>(header,16);
    if (!pointer(data) || !capacity || capacity>1024) return 0;
    std::vector<std::byte> entries(capacity*32);
    if (!process().copy(data,entries.data(),entries.size())) return 0;
    for (std::size_t i=0; i<capacity; ++i) {
        const auto name=detail::field<std::uintptr_t>(entries,i*32+16);
        const auto object=detail::field<std::uintptr_t>(entries,i*32+24);
        if (pointer(name) && pointer(object) && process().load_text(name,64)==wanted) return object;
    }
    return 0;
}

bool crosshair_dimensions(float& width, float& height)
{
    struct cached_hud { std::uintptr_t object{}, vtable{}; std::chrono::steady_clock::time_point refresh{}; };
    static thread_local cached_hud cached;
    const auto now=std::chrono::steady_clock::now();
    if (now>=cached.refresh || !cached.object
        || process().load<std::uintptr_t>(cached.object)!=cached.vtable) {
        cached.object=hud_element("CCSGO_HudReticle");
        cached.vtable=process().load<std::uintptr_t>(cached.object);
        cached.refresh=now+std::chrono::seconds(1);
    }
    if (!cached.object || !inside(cached.vtable)) return false;
    const auto locator=process().load<std::uintptr_t>(cached.vtable-8);
    if (!inside(locator,24)) return false;
    const auto adjustment=process().load<std::uint32_t>(locator+4);
    if (adjustment>0x100) return false;
    const auto ui=process().load<std::uintptr_t>(cached.object-adjustment+8);
    const auto offsets=panel(ui);
    if (!offsets) return false;
    if (!process().copy(ui+offsets.width,&width,sizeof(width))
        || !process().copy(ui+offsets.height,&height,sizeof(height))) return false;
    return std::isfinite(width) && std::isfinite(height)
        && width>=320 && height>=240 && width<=16384 && height<=16384;
}
bool crosshair_available() { return crosshair_source().buffers!=0; }
bool read_crosshair(crosshair_frame& frame)
{
    frame={};
    const auto& source=crosshair_source();
    if (!source.buffers) return false;
    std::uint32_t sequence{};
    if (!detail::coherent_frame(source.sequence,source.buffers,
        [](auto address,void* output,std::size_t size){return process().copy(address,output,size);},
        frame,sequence)) return false;
    static thread_local std::uint32_t last_sequence{};
    static thread_local auto last_change=std::chrono::steady_clock::time_point{};
    const auto now=std::chrono::steady_clock::now();
    if (sequence!=last_sequence) { last_sequence=sequence; last_change=now; }
    if (frame.count && now-last_change>std::chrono::milliseconds(250)) {frame={};return false;}
    return true;
}

int report(const char* path)
{
    std::ofstream out(path,std::ios::trunc);
    if (!out) return 3;
    out<<std::unitbuf;
    out<<"compatibility-report v1\n";
    if (!process().attach(L"cs2.exe") || !app::context().modules.discover(process())) {
        out<<"process=unavailable\n";return 2;
    }
    bool ok=true;
    const auto check=[&](std::string_view name,bool passed) {
        out<<name<<'='<<(passed?"PASS":"FAIL")<<'\n'; ok &= passed;
    };
    check("schema",game::fields().initialize());
    out<<"schema.fields="<<game::fields().field_count()<<'\n';
    check("globals",app::context().addresses.initialize());
    out<<"spread.cache.signature="<<(spread_patterns()?"PASS":"UNAVAILABLE (optional)")<<'\n';
    const auto layout=radar();
    check("radar.transform",layout.has_value());
    if (layout) out<<"radar.origin=0x"<<std::hex<<layout->origin
        <<" alternate=0x"<<layout->alternate_scale<<std::dec<<'\n';
    const auto object=hud_element("CCSGO_HudRadar");
    out<<"radar.hud="<<(object?"present":"not-loaded")<<'\n';
    if (object && layout) {
        const auto origin=process().load<foundation::vec2>(object+layout->origin);
        const auto scale=process().load<float>(object+layout->origin+0x20);
        out<<"radar.origin.xy="<<origin.x<<','<<origin.y<<" inverse_scale="<<scale<<'\n';
        bool found{};
        for(std::size_t off=0x220;off<0x440;off+=8) {
            const auto wrapper=process().load<std::uintptr_t>(object+off);
            if(!pointer(wrapper)) continue;
            const auto ui=process().load<std::uintptr_t>(wrapper+8);
            const auto p=panel(ui);
            if (!p) continue;
            const auto w=process().load<float>(ui+p.width);
            if(!std::isfinite(w)||w<200||w>1000) continue;
            out<<"panorama.width=0x"<<std::hex<<p.width<<" x=0x"<<p.x<<std::dec<<" value="<<w<<'\n';
            found=true;break;
        }
        check("panorama.getters",found);
    }
    check("crosshair.publication",crosshair_available());
    float width{},height{};
    check("crosshair.dimensions",crosshair_dimensions(width,height));
    out<<"crosshair.viewport="<<width<<','<<height<<'\n';
    if(object && width>0 && height>0)
        check("radar.live-layout",features::visuals::radar_t::diagnose(width,height,out));
    crosshair_frame frame{};
    out<<"crosshair.snapshot="<<(read_crosshair(frame)?"coherent":"not-published")
        <<" pieces="<<frame.count<<'\n';
    for(const auto name:{"cl_crosshair_screen_height","cl_crosshair_gap","cl_crosshair_length",
        "cl_crosshair_thickness","cl_crosshaircolor_a","cl_crosshairstyle",
        "cl_crosshaircolor_r","cl_crosshaircolor_g","cl_crosshaircolor_b"}) {
        int value{};const auto valid=game::variables().try_get(game::variables().find(identity::of(name)),value);
        check(name,valid);out<<"value="<<value<<'\n';
    }
    for(const auto name:{"cl_crosshairdot","cl_crosshair_t","cl_crosshair_drawoutline",
        "sv_accelerate_use_weapon_speed"}) {
        bool value{};
        check(name,game::variables().try_get(game::variables().find(identity::of(name)),value));
    }
    float forced{};
    check("weapon_accuracy_forcespread",game::variables().try_get(
        game::variables().find(identity::of("weapon_accuracy_forcespread")),forced));
    out<<"forcespread="<<forced<<'\n';
    config::publish_runtime_snapshot();
    game::local_player().update();
    for(int attempt=0;attempt<10;++attempt) {
        simulation::ballistics().tick();
        if(simulation::ballistics().ctx().valid) break;
        ::Sleep(2);
    }
    const auto ctx=simulation::ballistics().ctx();
    out<<"weapon.context="<<(ctx.valid?"valid":"not-ready")<<" item="<<ctx.item_def_idx
        <<" inaccuracy="<<ctx.inaccuracy<<" spread="<<ctx.spread<<'\n';
    if(ctx.valid && game::rules::is_firearm(ctx.weapon_type)) {
        const auto sample=simulation::ballistics().sample_spread_offset(1337,ctx.inaccuracy,
            ctx.spread,ctx.recoil_index,ctx.item_def_idx,ctx.fire_mode,ctx.num_bullets,0,ctx.pattern_seed);
        check("spread.live-sample",std::isfinite(sample.x)&&std::isfinite(sample.y));
        out<<"spread.xy="<<sample.x<<','<<sample.y<<'\n';
    }
    for(std::size_t i=0;i<frame.count;++i) {
        const auto& p=frame.pieces[i];
        out<<"crosshair.piece="<<p.type;
        for(float v:p.geometry) out<<','<<v;
        for(float v:p.color) out<<','<<v;
        out<<'\n';
    }
    out<<"result="<<(ok?"PASS":"FAIL")<<'\n';
    return ok?0:2;
}
} // namespace game::compatibility
