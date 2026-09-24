#pragma once
#include <render/menu/menu.hpp>
#include <render/menu/geometry.hpp>
#include <render/menu/localization.hpp>
#include <imgui_internal.h>
#include <d3dcompiler.h>

namespace render::menu::detail
{
class callback_ref
{
  public:
    template <class F>
    callback_ref(const F &callable)
        : m_object(&callable), m_call([](const void *object) { (*static_cast<const F *>(object))(); })
    {
    }
    void operator()() const
    {
        m_call(m_object);
    }

  private:
    const void *m_object;
    void (*m_call)(const void *);
};
inline ImVec4 k_bg_base{13.0f / 255.0f, 13.0f / 255.0f, 18.0f / 255.0f, 0.85f};

inline ImVec4 k_bg_panel{20.0f / 255.0f, 20.0f / 255.0f, 26.0f / 255.0f, 0.70f};

inline ImVec4 k_bg_card{28.0f / 255.0f, 28.0f / 255.0f, 36.0f / 255.0f, 0.60f};

inline ImVec4 k_bg_popup{13.0f / 255.0f, 13.0f / 255.0f, 18.0f / 255.0f, 0.78f};

inline ImVec4 k_bg_hover{1.0f, 1.0f, 1.0f, 0.08f};

inline ImVec4 k_accent{124.0f / 255.0f, 58.0f / 255.0f, 237.0f / 255.0f, 1.0f};

inline ImVec4 k_text_main{248.0f / 255.0f, 248.0f / 255.0f, 242.0f / 255.0f, 1.0f};

inline ImVec4 k_text_muted{161.0f / 255.0f, 161.0f / 255.0f, 170.0f / 255.0f, 1.0f};

inline ImVec4 k_border{1.0f, 1.0f, 1.0f, 0.06f};

inline ImVec4 k_border_light{1.0f, 1.0f, 1.0f, 0.12f};

inline constexpr float k_menu_width = 950.0f;

inline constexpr float k_menu_height = 650.0f;

[[nodiscard]] ImVec4 menu_color(const zdraw::rgba color);

void synchronize_menu_palette();

[[nodiscard]] float current_menu_scale(const float display_width, const float display_height);

[[nodiscard]] ImVec2 menu_transform_origin(const float display_width, const float display_height);

[[nodiscard]] ImVec2 initial_menu_layout_position(const float display_width, const float display_height);

class menu_render_scope final
{
  public:
    menu_render_scope(const ImVec2 display, const ImVec2 origin, const float scale)
        : m_viewport(ImGui::GetMainViewport()), m_scale(std::max(scale, 0.01f))
    {
        if (!m_viewport || !GImGui)
            return;

        m_viewport_pos = m_viewport->Pos;
        m_viewport_size = m_viewport->Size;
        m_work_pos = m_viewport->WorkPos;
        m_work_size = m_viewport->WorkSize;
        m_framebuffer_scale = m_viewport->FramebufferScale;
        m_fullscreen_clip = GImGui->DrawListSharedData.ClipRectFullscreen;
        m_font_density = ImGui::GetFontRasterizerDensity();

        const auto inverse = [&](const ImVec2 point) {
            return ImVec2{origin.x + (point.x - origin.x) / m_scale,
                          origin.y + (point.y - origin.y) / m_scale};
        };
        const auto virtual_min = inverse({0.0f, 0.0f});
        const auto virtual_max = inverse(display);
        const auto work_min = inverse(m_work_pos);
        const auto work_max = inverse({m_work_pos.x + m_work_size.x, m_work_pos.y + m_work_size.y});

        m_viewport->Pos = virtual_min;
        m_viewport->Size = {virtual_max.x - virtual_min.x, virtual_max.y - virtual_min.y};
        m_viewport->WorkPos = work_min;
        m_viewport->WorkSize = {work_max.x - work_min.x, work_max.y - work_min.y};
        // Begin()/BeginChild() derive rasterizer density from this field, so
        // SetFontRasterizerDensity() alone would be overwritten immediately.
        m_viewport->FramebufferScale = {m_scale, m_scale};
        GImGui->DrawListSharedData.ClipRectFullscreen = {virtual_min.x, virtual_min.y, virtual_max.x,
                                                         virtual_max.y};
        ImGui::SetFontRasterizerDensity(m_scale);
        m_active = true;
    }

    ~menu_render_scope()
    {
        if (!m_active)
            return;
        GImGui->DrawListSharedData.ClipRectFullscreen = m_fullscreen_clip;
        m_viewport->Pos = m_viewport_pos;
        m_viewport->Size = m_viewport_size;
        m_viewport->WorkPos = m_work_pos;
        m_viewport->WorkSize = m_work_size;
        m_viewport->FramebufferScale = m_framebuffer_scale;
        ImGui::SetFontRasterizerDensity(m_font_density);
    }

  private:
    ImGuiViewport *m_viewport{};
    float m_scale{1.0f};
    float m_font_density{1.0f};
    ImVec2 m_viewport_pos{};
    ImVec2 m_viewport_size{};
    ImVec2 m_work_pos{};
    ImVec2 m_work_size{};
    ImVec2 m_framebuffer_scale{};
    ImVec4 m_fullscreen_clip{};
    bool m_active{};
};

[[nodiscard]] bool belongs_to_menu(ImGuiWindow *window);

void scale_menu_draw_lists(const ImVec2 origin, const float scale);

struct preview_hitbox_geometry
{
    int bone{};
    ImVec2 from{};
    ImVec2 to{};
    float radius{};
};

inline constexpr std::array<int, 19> k_preview_skeleton_bones{1,  2,  3,  4,  6,  7,  9,  10, 11, 13,
                                                              14, 15, 17, 18, 19, 20, 21, 22, 23};

std::array<ImVec2, 24> make_preview_bones();

inline const auto k_preview_bones = make_preview_bones();

inline bool g_preview_orbiting{};

inline const std::array<preview_hitbox_geometry, 21> k_preview_hitboxes{{
    {1, {0.414286f, 0.419048f}, {0.575000f, 0.419048f}, 24.0f},
    {2, {0.432143f, 0.340476f}, {0.567857f, 0.342857f}, 27.0f},
    {3, {0.428571f, 0.290476f}, {0.571429f, 0.290476f}, 27.0f},
    {4, {0.407143f, 0.214286f}, {0.571429f, 0.211905f}, 25.0f},
    {6, {0.457143f, 0.169048f}, {0.528571f, 0.166667f}, 13.8f},
    {7, {0.478571f, 0.083333f}, {0.492857f, 0.121429f}, 17.5f},
    {8, {0.307143f, 0.285714f}, {0.357143f, 0.200000f}, 13.8f},
    {9, {0.282143f, 0.297619f}, {0.264286f, 0.390476f}, 9.4f},
    {10, {0.267857f, 0.397619f}, {0.257143f, 0.442857f}, 7.3f},
    {11, {0.264286f, 0.428571f}, {0.267857f, 0.445238f}, 5.0f},
    {12, {0.642857f, 0.180952f}, {0.717857f, 0.276190f}, 10.1f},
    {13, {0.717857f, 0.278571f}, {0.732143f, 0.390476f}, 9.4f},
    {14, {0.735714f, 0.404762f}, {0.750000f, 0.438095f}, 5.5f},
    {15, {0.735714f, 0.421429f}, {0.732143f, 0.435714f}, 5.0f},
    {17, {0.414286f, 0.397619f}, {0.400000f, 0.557143f}, 17.1f},
    {18, {0.403571f, 0.576190f}, {0.432143f, 0.719048f}, 15.3f},
    {19, {0.453571f, 0.740476f}, {0.385714f, 0.764286f}, 8.7f},
    {20, {0.585714f, 0.426190f}, {0.628571f, 0.566667f}, 14.2f},
    {21, {0.639286f, 0.576190f}, {0.682143f, 0.738095f}, 12.5f},
    {22, {0.678571f, 0.752381f}, {0.696429f, 0.778571f}, 10.3f},
    {23, {0.490f, 0.255f}, {0.490f, 0.195f}, 18.0f},
}};

struct toggle_animation
{
    float position{};
    float velocity{};
    float reveal{};
};

inline std::unordered_map<ImGuiID, toggle_animation> g_toggle_animations{};

inline std::unordered_map<ImGuiID, float> g_slider_animations{};

struct slider_drag_state
{
    bool dragging{};
    bool pressed_on_value{};
    double pixel_remainder{};
};

inline std::unordered_map<ImGuiID, slider_drag_state> g_slider_drags{};

struct slider_edit_state
{
    ImGuiID id{};
    std::array<char, 32> text{};
    bool request_focus{};
    int last_seen_frame{};
};

inline slider_edit_state g_slider_edit{};

inline std::unordered_map<ImGuiID, float> g_card_height_animations{};

inline std::unordered_map<ImGuiID, float> g_hover_animations{};

inline std::unordered_map<ImGuiID, float> g_active_animations{};

inline std::unordered_map<ImGuiID, std::array<char, 10>> g_color_hex{};

inline std::unordered_map<ImGuiID, float> g_color_hues{};

inline int *g_listening_key{};

inline int g_listening_start_frame{};

inline bool g_listening_armed{};

inline std::vector<float> g_row_start_y_stack{};

inline constexpr auto k_row_height = 42.0f;

inline ImVec2 g_menu_min{};

inline ImVec2 g_menu_max{};

inline ImVec2 g_settings_bounds_min{};

inline ImVec2 g_settings_bounds_max{};

inline bool g_settings_bounds_override{};

[[nodiscard]] ImVec2 settings_bounds_min();

[[nodiscard]] ImVec2 settings_bounds_max();

class settings_bounds_scope final
{
  public:
    settings_bounds_scope(ImVec2 min, ImVec2 max)
        : m_min(g_settings_bounds_min), m_max(g_settings_bounds_max), m_override(g_settings_bounds_override)
    {
        g_settings_bounds_min = min;
        g_settings_bounds_max = max;
        g_settings_bounds_override = true;
    }
    ~settings_bounds_scope()
    {
        g_settings_bounds_min = m_min;
        g_settings_bounds_max = m_max;
        g_settings_bounds_override = m_override;
    }

  private:
    ImVec2 m_min{};
    ImVec2 m_max{};
    bool m_override{};
};

inline ImVec2 g_cards_origin{};

inline float g_cards_y[2]{};

inline float g_cards_width{};

inline int g_cards_index{};

void color_picker_popup(zdraw::rgba &color, ImVec2 anchor_min, ImVec2 anchor_max, ImGuiID picker_id);

ImU32 packed(const ImVec4 &color);

struct popup_blur_request
{
    float logical_width{};
    float logical_height{};
    float rounding{};
    float radius{};
};

struct popup_blur_resources
{
    ID3D11Device *device{};
    ID3D11VertexShader *vertex_shader{};
    ID3D11PixelShader *pixel_shader{};
    ID3D11Buffer *constants{};
    ID3D11Texture2D *copy{};
    ID3D11ShaderResourceView *copy_view{};
    UINT width{};
    UINT height{};
    DXGI_FORMAT format{DXGI_FORMAT_UNKNOWN};

    ~popup_blur_resources()
    {
        release_all();
    }

    template <typename value_t> static void release(value_t *&value)
    {
        if (value)
            value->Release();
        value = nullptr;
    }

    void release_texture()
    {
        release(copy_view);
        release(copy);
        width = height = 0;
        format = DXGI_FORMAT_UNKNOWN;
    }

    void release_all()
    {
        release_texture();
        release(constants);
        release(pixel_shader);
        release(vertex_shader);
        release(device);
    }

    bool ensure_shaders(ID3D11Device *requested)
    {
        if (device != requested)
        {
            release_all();
            device = requested;
            if (device)
                device->AddRef();
        }
        if (!device)
            return false;
        if (vertex_shader && pixel_shader && constants)
            return true;

        static constexpr char vertex_source[] = R"(
struct output_t { float4 position : SV_POSITION; };
output_t main(uint id : SV_VertexID)
{
    float2 uv = float2((id << 1) & 2, id & 2);
    output_t output;
    output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
})";
        static constexpr char pixel_source[] = R"(
cbuffer blur_data : register(b0)
{
    float4 rect;
    float4 texel_round_radius;
};
Texture2D source_texture : register(t0);
SamplerState linear_sampler : register(s0);

float4 main(float4 position : SV_POSITION) : SV_Target
{
    float2 center = (rect.xy + rect.zw) * 0.5;
    float2 half_size = (rect.zw - rect.xy) * 0.5;
    float rounding = min(texel_round_radius.z, min(half_size.x, half_size.y));
    float2 q = abs(position.xy - center) - (half_size - rounding);
    float distance = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - rounding;
    clip(0.5 - distance);

    static const float weights[5] = { 1.0, 4.0, 6.0, 4.0, 1.0 };
    float2 uv = position.xy * texel_round_radius.xy;
    float2 step_uv = texel_round_radius.xy * texel_round_radius.w;
    float4 result = 0.0;
    [unroll] for (int y = -2; y <= 2; ++y)
    {
        [unroll] for (int x = -2; x <= 2; ++x)
            result += source_texture.Sample(linear_sampler, uv + float2(x, y) * step_uv)
                * weights[x + 2] * weights[y + 2];
    }
    return result / 256.0;
})";

        ID3DBlob *vertex_blob{};
        ID3DBlob *pixel_blob{};
        if (FAILED(D3DCompile(vertex_source, sizeof(vertex_source) - 1, nullptr, nullptr, nullptr, "main",
                              "vs_4_0", 0, 0, &vertex_blob, nullptr)))
            return false;
        if (FAILED(D3DCompile(pixel_source, sizeof(pixel_source) - 1, nullptr, nullptr, nullptr, "main",
                              "ps_4_0", 0, 0, &pixel_blob, nullptr)))
        {
            vertex_blob->Release();
            return false;
        }
        const auto vertex_ok = SUCCEEDED(device->CreateVertexShader(
            vertex_blob->GetBufferPointer(), vertex_blob->GetBufferSize(), nullptr, &vertex_shader));
        const auto pixel_ok = SUCCEEDED(device->CreatePixelShader(
            pixel_blob->GetBufferPointer(), pixel_blob->GetBufferSize(), nullptr, &pixel_shader));
        vertex_blob->Release();
        pixel_blob->Release();
        if (!vertex_ok || !pixel_ok)
            return false;

        D3D11_BUFFER_DESC description{};
        description.ByteWidth = 32;
        description.Usage = D3D11_USAGE_DYNAMIC;
        description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        return SUCCEEDED(device->CreateBuffer(&description, nullptr, &constants));
    }

    bool ensure_texture(const D3D11_TEXTURE2D_DESC &source)
    {
        if (source.SampleDesc.Count != 1)
            return false;
        if (copy && width == source.Width && height == source.Height && format == source.Format)
            return true;
        release_texture();
        auto description = source;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        description.CPUAccessFlags = 0;
        description.MiscFlags = 0;
        if (FAILED(device->CreateTexture2D(&description, nullptr, &copy)) ||
            FAILED(device->CreateShaderResourceView(copy, nullptr, &copy_view)))
        {
            release_texture();
            return false;
        }
        width = source.Width;
        height = source.Height;
        format = source.Format;
        return true;
    }
};

inline popup_blur_resources g_popup_blur{};

void popup_blur_callback(const ImDrawList *, const ImDrawCmd *command);

ImVec4 to_imvec(const zdraw::rgba &color);

void from_imvec(const ImVec4 &value, zdraw::rgba &color);

float approach(float current, float target, float duration);

ImVec4 mix(const ImVec4 &from, const ImVec4 &to, float amount);

float animate_state(std::unordered_map<ImGuiID, float> &animations, ImGuiID id, bool enabled,
                    float duration = 0.30f);

float animate_selection(ImGuiID id, bool active);

void spring_to(float &position, float &velocity, float target);

void pointer_cursor_if_hovered();

void soft_shadow(ImDrawList *draw, ImVec2 min, ImVec2 max, float rounding, ImVec2 offset, float spread,
                 ImVec4 tint, float strength);

void add_vertical_gradient_rounded(ImDrawList *draw, ImVec2 min, ImVec2 max, ImU32 top, ImU32 bottom,
                                   float rounding, ImDrawFlags corners);

void draw_popup_surface(ImDrawList *draw, ImVec2 min, ImVec2 max, float rounding);

void add_linear_gradient_rounded(ImDrawList *draw, ImVec2 min, ImVec2 max, ImU32 from, ImU32 to,
                                 float rounding, ImDrawFlags corners, bool horizontal);

void push_menu_font(zdraw::font *font);

std::size_t utf8_decode(const char *begin, const char *end, unsigned int &out);

void draw_section_title(ImDrawList *draw, ImVec2 position, float line_end_x, std::string_view title,
                        bool uppercase = false);

void begin_row(const char *label, float control_width);

void clipped_row_text(const std::string_view text, const ImVec4 color = k_text_muted);

void end_row();

void toggle_control(bool &value);

void toggle_row(const char *label, bool &value);

void toggle_color_row(const char *label, bool &value, zdraw::rgba &color);

template <typename value_t>
void slider_row_impl(const char *label, value_t &value, value_t minimum, value_t maximum, const char *suffix,
                     value_t step)
{
    constexpr auto control_width = 130.0f;
    begin_row(label, control_width);
    const auto id = ImGui::GetID("##slider");
    const auto range = static_cast<double>(maximum) - static_cast<double>(minimum);
    const auto quantum = std::max(static_cast<double>(step), std::numeric_limits<double>::epsilon());
    const auto decimal_places = [](double increment) {
        if (increment >= 1.0)
            return 0;
        auto scaled = increment;
        for (int digits = 1; digits <= 4; ++digits)
        {
            scaled *= 10.0;
            if (std::abs(scaled - std::round(scaled)) < 0.00001)
                return digits;
        }
        return 4;
    };
    const auto digits = std::is_integral_v<value_t> ? 0 : decimal_places(quantum);
    const auto format_value = [&](char *output, std::size_t size, bool include_suffix) {
        if constexpr (std::is_integral_v<value_t>)
            std::snprintf(output, size, include_suffix ? "%d%s" : "%d", static_cast<int>(value), suffix);
        else
            std::snprintf(output, size, include_suffix ? "%.*f%s" : "%.*f", digits,
                          static_cast<double>(value), suffix);
    };
    const auto assign = [&](double raw) {
        raw = static_cast<double>(minimum) +
              std::round((raw - static_cast<double>(minimum)) / quantum) * quantum;
        value =
            static_cast<value_t>(std::clamp(raw, static_cast<double>(minimum), static_cast<double>(maximum)));
    };

    if (g_slider_edit.id && g_slider_edit.last_seen_frame < GImGui->FrameCount - 1)
        g_slider_edit = {};

    char value_text[48]{};
    format_value(value_text, sizeof(value_text), true);
    const auto text_size = ImGui::CalcTextSize(value_text);
    ImGui::InvisibleButton("##slider", {control_width, 28.0f});
    pointer_cursor_if_hovered();
    const auto item_min = ImGui::GetItemRectMin();
    const auto item_max = ImGui::GetItemRectMax();
    const auto actual_width = std::max(1.0f, item_max.x - item_min.x);
    const auto track_a = ImVec2{item_min.x + 7.0f, item_min.y + 22.0f};
    const auto track_b = ImVec2{item_max.x - 7.0f, item_min.y + 26.0f};
    const auto track_width = std::max(1.0f, track_b.x - track_a.x);
    const auto value_rect = ImRect{{item_max.x - text_size.x - 5.0f, item_min.y - 3.0f},
                                   {item_max.x + 3.0f, item_min.y + text_size.y + 4.0f}};
    const auto value_hovered = value_rect.Contains(ImGui::GetIO().MousePos);
    const auto active = ImGui::IsItemActive();
    auto &drag = g_slider_drags[id];

    if (value_hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        g_slider_edit = {};
        g_slider_edit.id = id;
        g_slider_edit.request_focus = true;
        g_slider_edit.last_seen_frame = GImGui->FrameCount;
        format_value(g_slider_edit.text.data(), g_slider_edit.text.size(), false);
        drag = {};
        ImGui::ClearActiveID();
    }

    const auto editing = g_slider_edit.id == id;
    if (active && ImGui::IsItemActivated())
    {
        drag.pressed_on_value = value_hovered;
        drag.dragging = !drag.pressed_on_value && !editing;
        drag.pixel_remainder = 0.0;
        if (drag.dragging)
        {
            // A press on the track is an absolute seek; continued movement switches
            // to the adaptive relative grid below.
            const auto fraction =
                std::clamp((ImGui::GetIO().MousePos.x - track_a.x) / track_width, 0.0f, 1.0f);
            assign(static_cast<double>(minimum) + fraction * range);
        }
    }
    else if (active && drag.dragging && !editing)
    {
        const auto intervals = range > 0.0 ? std::max(1.0, std::round(range / quantum)) : 1.0;
        const auto pixels_per_step = std::max(1.0, static_cast<double>(track_width) / intervals);
        drag.pixel_remainder += static_cast<double>(ImGui::GetIO().MouseDelta.x);
        const auto steps = std::trunc(drag.pixel_remainder / pixels_per_step);
        if (steps != 0.0)
        {
            assign(static_cast<double>(value) + steps * quantum);
            drag.pixel_remainder -= steps * pixels_per_step;
        }
    }
    else if (!active)
    {
        drag = {};
    }

    const auto target =
        range > 0.0 ? static_cast<float>((static_cast<double>(value) - static_cast<double>(minimum)) / range)
                    : 0.0f;
    auto &shown = g_slider_animations[id];
    if (shown == 0.0f && target > 0.0f)
    {
        shown = target;
    }
    // The thumb follows direct manipulation without easing. External config
    // changes keep the existing soft transition.
    shown = active && drag.dragging ? target : approach(shown, target, 0.10f);

    auto *draw = ImGui::GetWindowDrawList();
    if (!editing)
        draw->AddText({item_max.x - text_size.x, item_min.y}, packed(k_text_muted), value_text);
    draw->AddRectFilled(track_a, track_b, IM_COL32(255, 255, 255, 26), 2.0f);
    const auto x = track_a.x + track_width * shown;
    draw->AddRectFilled(track_a, {x, track_b.y}, packed(k_accent), 2.0f);
    const auto radius = ImGui::IsItemHovered() || (active && drag.dragging) ? 8.5f : 7.0f;
    draw->AddCircleFilled({x, item_min.y + 24.0f}, radius, IM_COL32_WHITE);
    draw->AddCircle({x, item_min.y + 24.0f}, radius, IM_COL32(0, 0, 0, 100));

    if (editing)
    {
        g_slider_edit.last_seen_frame = GImGui->FrameCount;
        const auto editor_width = std::clamp(text_size.x + 18.0f, 52.0f, actual_width);
        ImGui::SetCursorScreenPos({item_max.x - editor_width, item_min.y - 3.0f});
        ImGui::PushStyleColor(ImGuiCol_FrameBg, k_bg_panel);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, k_bg_hover);
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, k_bg_hover);
        ImGui::PushStyleColor(ImGuiCol_Border, k_border_light);
        ImGui::PushStyleColor(ImGuiCol_Text, k_text_main);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {6.0f, 2.0f});
        ImGui::SetNextItemWidth(editor_width);
        if (g_slider_edit.request_focus)
        {
            ImGui::SetKeyboardFocusHere();
            g_slider_edit.request_focus = false;
        }
        const auto enter =
            ImGui::InputText("##slider_value_input", g_slider_edit.text.data(), g_slider_edit.text.size(),
                             ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue);
        const auto escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        const auto deactivated = ImGui::IsItemDeactivated();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(5);

        if (escape)
        {
            g_slider_edit = {};
        }
        else if (enter || deactivated)
        {
            auto normalized = std::string{g_slider_edit.text.data()};
            std::ranges::replace(normalized, ',', '.');
            char *parsed_end{};
            const auto parsed = std::strtod(normalized.c_str(), &parsed_end);
            while (parsed_end && *parsed_end && std::isspace(static_cast<unsigned char>(*parsed_end)))
                ++parsed_end;
            if (parsed_end != normalized.c_str() && parsed_end && *parsed_end == '\0' &&
                std::isfinite(parsed))
            {
                assign(parsed);
            }
            g_slider_edit = {};
        }
    }

    end_row();
}

void slider_row(const char *label, int &value, int minimum, int maximum, const char *suffix = "");

void slider_row(const char *label, float &value, float minimum, float maximum, const char *suffix,
                float step);

void slider_percent_row(const char *label, float &value);

void draw_checkmark(ImDrawList *draw, ImVec2 center, ImU32 color, float amount, float thickness = 1.7f);

void draw_dropdown_chevron(ImDrawList *draw, ImVec2 center, ImU32 color, float open_amount);

void select_row(const char *label, int &value, std::span<const char *const> options);

void multiselect_row(const char *label, int &mask, std::span<const std::pair<const char *, int>> options,
                     int all_mask);

void aim_parts_row(int &mask);

enum class row_action_icon
{
    none,
    copy,
    check,
    save,
    folder
};

void draw_action_icon(ImDrawList *draw, row_action_icon icon, ImVec2 center, ImU32 color);

bool button_row(const char *label, const char *text, row_action_icon icon = row_action_icon::none);

int filter_filename_char(ImGuiInputTextCallbackData *data);

void text_input_row(const char *label, char *buffer, std::size_t size);

std::string key_name(int key);

[[nodiscard]] bool bind_input_is_down();

[[nodiscard]] int pressed_bind_key();

void keybind_row(const char *label, int &value);

void color_picker_popup(zdraw::rgba &color, ImVec2 item_min, ImVec2 item_max, ImGuiID picker_id);

void color_row(const char *label, zdraw::rgba &color);

void humanizer_preview(int amount, int smoothing, const config::combat_profile::humanizer_settings &settings);

void settings_popup(ImVec2 anchor_min, ImVec2 anchor_max, int rows, callback_ref callback);

void settings_popup_row(const char *label, int rows, callback_ref callback);

void toggle_popup_row(const char *label, bool &value, int rows, callback_ref callback);

inline constexpr int k_bar_settings_rows = 7;

inline constexpr int k_weapon_settings_rows = 4;

inline constexpr int k_info_flag_settings_rows = 4;

void dock_player_bar(config::visual_profile::player::layout_element &layout, int dock);

template <typename bar_t>
void player_bar_settings_rows(bar_t &bar, config::visual_profile::player::layout_element &layout)
{
    int position = static_cast<int>(bar.position);
    const auto previous_position = position;
    static constexpr const char *positions[]{"Left", "Top", "Bottom", "Right"};
    select_row("Position", position, positions);
    bar.position = static_cast<typename bar_t::position_type>(position);
    if (position != previous_position)
        dock_player_bar(layout, position);
    slider_row("Thickness", bar.thickness, 1.0f, 12.0f, " px", 0.5f);
    toggle_popup_row("Outline", bar.outline, 2, [&] {
        slider_row("Thickness", bar.outline_thickness, 0.5f, 4.0f, " px", 0.5f);
        color_row("Color", bar.outline_color);
    });
    toggle_popup_row("Gradient", bar.gradient, 2, [&] {
        color_row("Full Color", bar.full_color);
        color_row("Low Color", bar.low_color);
    });
    bool segmented = bar.segments > 1;
    toggle_popup_row("Segments", segmented, 2, [&] {
        slider_row("Count", bar.segments, 2, 10);
        slider_row("Gap", bar.segment_gap, 0.0f, 4.0f, " px", 0.5f);
    });
    if (segmented && bar.segments < 2)
        bar.segments = 4;
    if (!segmented)
        bar.segments = 1;
    toggle_popup_row("Show Value", bar.show_value, 1, [&] { color_row("Text Color", bar.text_color); });
    color_row("Background Color", bar.background_color);
}

void player_weapon_settings_rows(config::visual_profile::player::weapon &weapon);

config::visual_profile::player::info_flags::style &selected_info_flag_style(
    config::visual_profile::player::info_flags &flags, int selected);

void player_info_flag_settings_rows(config::visual_profile::player::info_flags &flags);

void visual_editor_settings_popup(int rows, callback_ref callback);

inline constexpr const char *k_chams_materials[]{"Solid",      "Shaded",     "Glow",  "Glow Outline",
                                                 "Iridescent", "Water Flow", "Glossy"};

static_assert(std::size(k_chams_materials) == config::visual_profile::chams::material_type_count);

[[nodiscard]] int chams_material_row_count(const config::visual_profile::chams::material &m);

void chams_material_rows(config::visual_profile::chams::material &m);

void card_in_column(const char *id, const char *title, int rows, int column, callback_ref callback);

void card(const char *id, const char *title, int rows, callback_ref callback);

bool tab_button(const char *label, bool active);

ImVec2 svg_point(ImVec2 center, float x, float y);

void svg_segment(ImDrawList *draw, ImVec2 from, ImVec2 to, ImU32 color, float thickness = 1.65f);

void svg_polyline(ImDrawList *draw, std::span<const ImVec2> points, ImU32 color, bool closed = false,
                  float thickness = 1.65f);

void draw_nav_icon(ImDrawList *draw, int icon, ImVec2 center, ImU32 color);

bool nav_button(const char *label, bool active, int icon);

void begin_cards(const char *id);

void end_cards();
} // namespace render::menu::detail
