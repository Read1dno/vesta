#include <stdafx.hpp>
#include <scripting/runtime.hpp>
#include <app/context.hpp>
#include <core/input/bindings.hpp>
#include <core/input/hotkeys.hpp>
#include <features/visuals/visuals.hpp>
#include <features/visuals/hitsound.hpp>
#include <render/chams/preview.hpp>
#include <render/chams/renderer.hpp>
#include <render/menu/localization.hpp>
#include <render/menu/menu.hpp>
#include <render/overlay/input.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <d3d11.h>
#include <d3dcompiler.h>

namespace
{
	ImVec4 k_bg_base{ 13.0f / 255.0f, 13.0f / 255.0f, 18.0f / 255.0f, 0.85f };
	ImVec4 k_bg_panel{ 20.0f / 255.0f, 20.0f / 255.0f, 26.0f / 255.0f, 0.70f };
	ImVec4 k_bg_card{ 28.0f / 255.0f, 28.0f / 255.0f, 36.0f / 255.0f, 0.60f };

	ImVec4 k_bg_popup{ 13.0f / 255.0f, 13.0f / 255.0f, 18.0f / 255.0f, 0.78f };
	ImVec4 k_bg_hover{ 1.0f, 1.0f, 1.0f, 0.08f };
	ImVec4 k_accent{ 124.0f / 255.0f, 58.0f / 255.0f, 237.0f / 255.0f, 1.0f };
	ImVec4 k_text_main{ 248.0f / 255.0f, 248.0f / 255.0f, 242.0f / 255.0f, 1.0f };
	ImVec4 k_text_muted{ 161.0f / 255.0f, 161.0f / 255.0f, 170.0f / 255.0f, 1.0f };
	ImVec4 k_border{ 1.0f, 1.0f, 1.0f, 0.06f };
	ImVec4 k_border_light{ 1.0f, 1.0f, 1.0f, 0.12f };
	constexpr float k_menu_width = 950.0f;
	constexpr float k_menu_height = 650.0f;

	[[nodiscard]] ImVec4 menu_color( const zdraw::rgba color )
	{
		return { color.r / 255.0f, color.g / 255.0f,
			color.b / 255.0f, color.a / 255.0f };
	}

	void synchronize_menu_palette( )
	{
		const auto& palette = config::general_settings.palette;
		k_bg_base = menu_color( palette.background );
		k_bg_panel = menu_color( palette.panel );
		k_bg_card = menu_color( palette.card );
		k_bg_popup = menu_color( palette.popup );
		k_bg_hover = menu_color( palette.hover );
		k_accent = menu_color( palette.accent );
		k_text_main = menu_color( palette.text );
		k_text_muted = menu_color( palette.muted_text );
		k_border = menu_color( palette.border );
		k_border_light = k_border;
		k_border_light.w = std::min( 1.0f, k_border.w * 2.0f );
	}

	[[nodiscard]] float current_menu_scale(
		const float display_width, const float display_height )
	{
		const auto requested = std::clamp(
			config::general_settings.menu_scale, 0.50f, 1.50f );
		const auto fit = std::min(
			display_width / k_menu_width,
			display_height / k_menu_height );
		return std::min( requested, std::max( 0.35f, fit ) );
	}

	[[nodiscard]] ImVec2 menu_transform_origin(
		const float display_width, const float display_height )
	{
		return { display_width * 0.5f, display_height * 0.5f };
	}

	[[nodiscard]] ImVec2 initial_menu_layout_position(
		const float display_width, const float display_height )
	{
		return { ( display_width - k_menu_width ) * 0.5f,
			( display_height - k_menu_height ) * 0.5f };
	}

	class menu_render_scope final
	{
	public:
		menu_render_scope( const ImVec2 display, const ImVec2 origin,
			const float scale )
			: m_viewport( ImGui::GetMainViewport( ) ),
			  m_scale( std::max( scale, 0.01f ) )
		{
			if ( !m_viewport || !GImGui )
				return;

			m_viewport_pos = m_viewport->Pos;
			m_viewport_size = m_viewport->Size;
			m_work_pos = m_viewport->WorkPos;
			m_work_size = m_viewport->WorkSize;
			m_framebuffer_scale = m_viewport->FramebufferScale;
			m_fullscreen_clip = GImGui->DrawListSharedData.ClipRectFullscreen;
			m_font_density = ImGui::GetFontRasterizerDensity( );

			const auto inverse = [ & ]( const ImVec2 point )
			{
				return ImVec2{
					origin.x + ( point.x - origin.x ) / m_scale,
					origin.y + ( point.y - origin.y ) / m_scale };
			};
			const auto virtual_min = inverse( { 0.0f, 0.0f } );
			const auto virtual_max = inverse( display );
			const auto work_min = inverse( m_work_pos );
			const auto work_max = inverse( {
				m_work_pos.x + m_work_size.x,
				m_work_pos.y + m_work_size.y } );

			m_viewport->Pos = virtual_min;
			m_viewport->Size = {
				virtual_max.x - virtual_min.x,
				virtual_max.y - virtual_min.y };
			m_viewport->WorkPos = work_min;
			m_viewport->WorkSize = {
				work_max.x - work_min.x,
				work_max.y - work_min.y };

			m_viewport->FramebufferScale = { m_scale, m_scale };
			GImGui->DrawListSharedData.ClipRectFullscreen = {
				virtual_min.x, virtual_min.y, virtual_max.x, virtual_max.y };
			ImGui::SetFontRasterizerDensity( m_scale );
			m_active = true;
		}

		~menu_render_scope( )
		{
			if ( !m_active )
				return;
			GImGui->DrawListSharedData.ClipRectFullscreen = m_fullscreen_clip;
			m_viewport->Pos = m_viewport_pos;
			m_viewport->Size = m_viewport_size;
			m_viewport->WorkPos = m_work_pos;
			m_viewport->WorkSize = m_work_size;
			m_viewport->FramebufferScale = m_framebuffer_scale;
			ImGui::SetFontRasterizerDensity( m_font_density );
		}

	private:
		ImGuiViewport* m_viewport{};
		float m_scale{ 1.0f };
		float m_font_density{ 1.0f };
		ImVec2 m_viewport_pos{};
		ImVec2 m_viewport_size{};
		ImVec2 m_work_pos{};
		ImVec2 m_work_size{};
		ImVec2 m_framebuffer_scale{};
		ImVec4 m_fullscreen_clip{};
		bool m_active{};
	};

	[[nodiscard]] bool belongs_to_menu( ImGuiWindow* window )
	{
		for ( auto* current = window; current; current = current->ParentWindow )
		{
			const auto name = std::string_view{
				current->Name ? current->Name : "" };
			if ( name == "##vesta_native_menu" || name == "##esp_visual_editor" )
				return true;
		}
		return false;
	}

	void scale_menu_draw_lists( const ImVec2 origin, const float scale )
	{
		if ( std::abs( scale - 1.0f ) < 0.0001f || !GImGui )
			return;

		for ( auto* window : GImGui->Windows )
		{
			if ( !window || window->LastFrameActive != GImGui->FrameCount
				|| !belongs_to_menu( window ) || !window->DrawList )
			{
				continue;
			}

			for ( auto& vertex : window->DrawList->VtxBuffer )
			{
				vertex.pos.x = origin.x + ( vertex.pos.x - origin.x ) * scale;
				vertex.pos.y = origin.y + ( vertex.pos.y - origin.y ) * scale;
			}
			for ( auto& command : window->DrawList->CmdBuffer )
			{
				command.ClipRect.x = origin.x + ( command.ClipRect.x - origin.x ) * scale;
				command.ClipRect.y = origin.y + ( command.ClipRect.y - origin.y ) * scale;
				command.ClipRect.z = origin.x + ( command.ClipRect.z - origin.x ) * scale;
				command.ClipRect.w = origin.y + ( command.ClipRect.w - origin.y ) * scale;
			}
		}
	}

	struct preview_hitbox_geometry
	{
		int bone{};
		ImVec2 from{};
		ImVec2 to{};
		float radius{};
	};

	constexpr std::array<int, 19> k_preview_skeleton_bones{
		1, 2, 3, 4, 6, 7, 9, 10, 11, 13, 14, 15, 17, 18, 19, 20, 21, 22, 23 };

	std::array<ImVec2, 24> make_preview_bones( )
	{
		std::array<ImVec2, 24> result{};
		result[ 1 ] = { 0.485714f, 0.380952f };
		result[ 2 ] = { 0.489286f, 0.338095f };
		result[ 3 ] = { 0.489286f, 0.302381f };
		result[ 4 ] = { 0.489286f, 0.259524f };
		result[ 6 ] = { 0.496429f, 0.147619f };
		result[ 7 ] = { 0.482143f, 0.097619f };
		result[ 9 ] = { 0.335714f, 0.185714f };
		result[ 10 ] = { 0.282143f, 0.280952f };
		result[ 11 ] = { 0.267857f, 0.404762f };
		result[ 13 ] = { 0.650000f, 0.190476f };
		result[ 14 ] = { 0.717857f, 0.273810f };
		result[ 15 ] = { 0.739286f, 0.397619f };
		result[ 17 ] = { 0.400000f, 0.407143f };
		result[ 18 ] = { 0.389286f, 0.566667f };
		result[ 19 ] = { 0.432143f, 0.735714f };
		result[ 20 ] = { 0.582143f, 0.409524f };
		result[ 21 ] = { 0.639286f, 0.573810f };
		result[ 22 ] = { 0.689286f, 0.759524f };
		result[ 23 ] = { 0.492857f, 0.211905f };
		return result;
	}

	const auto k_preview_bones = make_preview_bones( );

	bool g_preview_orbiting{};
	const std::array<preview_hitbox_geometry, 21> k_preview_hitboxes{ {
		{ 1, { 0.414286f, 0.419048f }, { 0.575000f, 0.419048f }, 24.0f },
		{ 2, { 0.432143f, 0.340476f }, { 0.567857f, 0.342857f }, 27.0f },
		{ 3, { 0.428571f, 0.290476f }, { 0.571429f, 0.290476f }, 27.0f },
		{ 4, { 0.407143f, 0.214286f }, { 0.571429f, 0.211905f }, 25.0f },
		{ 6, { 0.457143f, 0.169048f }, { 0.528571f, 0.166667f }, 13.8f },
		{ 7, { 0.478571f, 0.083333f }, { 0.492857f, 0.121429f }, 17.5f },
		{ 8, { 0.307143f, 0.285714f }, { 0.357143f, 0.200000f }, 13.8f },
		{ 9, { 0.282143f, 0.297619f }, { 0.264286f, 0.390476f }, 9.4f },
		{ 10, { 0.267857f, 0.397619f }, { 0.257143f, 0.442857f }, 7.3f },
		{ 11, { 0.264286f, 0.428571f }, { 0.267857f, 0.445238f }, 5.0f },
		{ 12, { 0.642857f, 0.180952f }, { 0.717857f, 0.276190f }, 10.1f },
		{ 13, { 0.717857f, 0.278571f }, { 0.732143f, 0.390476f }, 9.4f },
		{ 14, { 0.735714f, 0.404762f }, { 0.750000f, 0.438095f }, 5.5f },
		{ 15, { 0.735714f, 0.421429f }, { 0.732143f, 0.435714f }, 5.0f },
		{ 17, { 0.414286f, 0.397619f }, { 0.400000f, 0.557143f }, 17.1f },
		{ 18, { 0.403571f, 0.576190f }, { 0.432143f, 0.719048f }, 15.3f },
		{ 19, { 0.453571f, 0.740476f }, { 0.385714f, 0.764286f }, 8.7f },
		{ 20, { 0.585714f, 0.426190f }, { 0.628571f, 0.566667f }, 14.2f },
		{ 21, { 0.639286f, 0.576190f }, { 0.682143f, 0.738095f }, 12.5f },
		{ 22, { 0.678571f, 0.752381f }, { 0.696429f, 0.778571f }, 10.3f },
		{ 23, { 0.490f, 0.255f }, { 0.490f, 0.195f }, 18.0f },
	} };

	struct toggle_animation
	{
		float position{};
		float velocity{};
		float reveal{};
	};

	std::unordered_map<ImGuiID, toggle_animation> g_toggle_animations{};
	std::unordered_map<ImGuiID, float> g_slider_animations{};
	struct slider_drag_state
	{
		bool dragging{};
		bool pressed_on_value{};
		double pixel_remainder{};
	};
	std::unordered_map<ImGuiID, slider_drag_state> g_slider_drags{};
	struct slider_edit_state
	{
		ImGuiID id{};
		std::array<char, 32> text{};
		bool request_focus{};
		int last_seen_frame{};
	};
	slider_edit_state g_slider_edit{};
	std::unordered_map<ImGuiID, float> g_card_height_animations{};
	std::unordered_map<ImGuiID, float> g_hover_animations{};
	std::unordered_map<ImGuiID, float> g_active_animations{};
	std::unordered_map<ImGuiID, std::array<char, 10>> g_color_hex{};
	std::unordered_map<ImGuiID, float> g_color_hues{};
	int* g_listening_key{};
	int g_listening_start_frame{};

	bool g_listening_armed{};
	std::vector<float> g_row_start_y_stack{};
	constexpr auto k_row_height = 42.0f;
	ImVec2 g_menu_min{};
	ImVec2 g_menu_max{};
	ImVec2 g_settings_bounds_min{};
	ImVec2 g_settings_bounds_max{};
	bool g_settings_bounds_override{};

	[[nodiscard]] ImVec2 settings_bounds_min( )
	{
		return g_settings_bounds_override ? g_settings_bounds_min : g_menu_min;
	}

	[[nodiscard]] ImVec2 settings_bounds_max( )
	{
		return g_settings_bounds_override ? g_settings_bounds_max : g_menu_max;
	}

	class settings_bounds_scope final
	{
	public:
		settings_bounds_scope( ImVec2 min, ImVec2 max )
			: m_min( g_settings_bounds_min ), m_max( g_settings_bounds_max ),
			  m_override( g_settings_bounds_override )
		{
			g_settings_bounds_min = min;
			g_settings_bounds_max = max;
			g_settings_bounds_override = true;
		}
		~settings_bounds_scope( )
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
	ImVec2 g_cards_origin{};
	float g_cards_y[ 2 ]{};
	float g_cards_width{};
	int g_cards_index{};

	void color_picker_popup( zdraw::rgba& color, ImVec2 anchor_min, ImVec2 anchor_max, ImGuiID picker_id );

	ImU32 packed( const ImVec4& color )
	{
		return ImGui::ColorConvertFloat4ToU32( color );
	}

	struct popup_blur_request
	{
		float logical_width{};
		float logical_height{};
		float rounding{};
		float radius{};
	};

	struct popup_blur_resources
	{
		ID3D11Device* device{};
		ID3D11VertexShader* vertex_shader{};
		ID3D11PixelShader* pixel_shader{};
		ID3D11Buffer* constants{};
		ID3D11Texture2D* copy{};
		ID3D11ShaderResourceView* copy_view{};
		UINT width{};
		UINT height{};
		DXGI_FORMAT format{ DXGI_FORMAT_UNKNOWN };

		~popup_blur_resources( ) { release_all( ); }

		template<typename value_t>
		static void release( value_t*& value )
		{
			if ( value ) value->Release( );
			value = nullptr;
		}

		void release_texture( )
		{
			release( copy_view );
			release( copy );
			width = height = 0;
			format = DXGI_FORMAT_UNKNOWN;
		}

		void release_all( )
		{
			release_texture( );
			release( constants );
			release( pixel_shader );
			release( vertex_shader );
			release( device );
		}

		bool ensure_shaders( ID3D11Device* requested )
		{
			if ( device != requested )
			{
				release_all( );
				device = requested;
				if ( device ) device->AddRef( );
			}
			if ( !device ) return false;
			if ( vertex_shader && pixel_shader && constants ) return true;

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

			ID3DBlob* vertex_blob{};
			ID3DBlob* pixel_blob{};
			if ( FAILED( D3DCompile( vertex_source, sizeof( vertex_source ) - 1, nullptr,
				nullptr, nullptr, "main", "vs_4_0", 0, 0, &vertex_blob, nullptr ) ) )
				return false;
			if ( FAILED( D3DCompile( pixel_source, sizeof( pixel_source ) - 1, nullptr,
				nullptr, nullptr, "main", "ps_4_0", 0, 0, &pixel_blob, nullptr ) ) )
			{
				vertex_blob->Release( );
				return false;
			}
			const auto vertex_ok = SUCCEEDED( device->CreateVertexShader(
				vertex_blob->GetBufferPointer( ), vertex_blob->GetBufferSize( ), nullptr, &vertex_shader ) );
			const auto pixel_ok = SUCCEEDED( device->CreatePixelShader(
				pixel_blob->GetBufferPointer( ), pixel_blob->GetBufferSize( ), nullptr, &pixel_shader ) );
			vertex_blob->Release( );
			pixel_blob->Release( );
			if ( !vertex_ok || !pixel_ok ) return false;

			D3D11_BUFFER_DESC description{};
			description.ByteWidth = 32;
			description.Usage = D3D11_USAGE_DYNAMIC;
			description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			return SUCCEEDED( device->CreateBuffer( &description, nullptr, &constants ) );
		}

		bool ensure_texture( const D3D11_TEXTURE2D_DESC& source )
		{
			if ( source.SampleDesc.Count != 1 ) return false;
			if ( copy && width == source.Width && height == source.Height && format == source.Format )
				return true;
			release_texture( );
			auto description = source;
			description.Usage = D3D11_USAGE_DEFAULT;
			description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			description.CPUAccessFlags = 0;
			description.MiscFlags = 0;
			if ( FAILED( device->CreateTexture2D( &description, nullptr, &copy ) ) ||
				FAILED( device->CreateShaderResourceView( copy, nullptr, &copy_view ) ) )
			{
				release_texture( );
				return false;
			}
			width = source.Width;
			height = source.Height;
			format = source.Format;
			return true;
		}
	};

	popup_blur_resources g_popup_blur{};

	void popup_blur_callback( const ImDrawList*, const ImDrawCmd* command )
	{
		if ( !command || command->UserCallbackDataSize != sizeof( popup_blur_request ) ) return;
		const auto* request = static_cast<const popup_blur_request*>( command->UserCallbackData );
		auto* render_state = static_cast<ImGui_ImplDX11_RenderState*>(
			ImGui::GetPlatformIO( ).Renderer_RenderState );
		if ( !request || !render_state || !render_state->Device || !render_state->DeviceContext ||
			!g_popup_blur.ensure_shaders( render_state->Device ) )
			return;

		auto* context = render_state->DeviceContext;
		ID3D11RenderTargetView* target{};
		context->OMGetRenderTargets( 1, &target, nullptr );
		if ( !target ) return;
		ID3D11Resource* resource{};
		ID3D11Texture2D* source{};
		target->GetResource( &resource );
		if ( resource ) resource->QueryInterface( IID_PPV_ARGS( &source ) );
		if ( resource ) resource->Release( );
		if ( !source )
		{
			target->Release( );
			return;
		}

		D3D11_TEXTURE2D_DESC source_description{};
		source->GetDesc( &source_description );
		if ( !g_popup_blur.ensure_texture( source_description ) )
		{
			source->Release( );
			target->Release( );
			return;
		}
		context->CopyResource( g_popup_blur.copy, source );

		const auto physical_width = std::max( 1.0f, command->ClipRect.z - command->ClipRect.x );
		const auto physical_height = std::max( 1.0f, command->ClipRect.w - command->ClipRect.y );
		const auto scale = std::min(
			physical_width / std::max( request->logical_width, 1.0f ),
			physical_height / std::max( request->logical_height, 1.0f ) );
		struct constants_t { float rect[ 4 ]; float texture[ 4 ]; } constants{
			{ command->ClipRect.x, command->ClipRect.y, command->ClipRect.z, command->ClipRect.w },
			{ 1.0f / source_description.Width, 1.0f / source_description.Height,
				request->rounding * scale, request->radius * scale } };
		D3D11_MAPPED_SUBRESOURCE mapped{};
		if ( SUCCEEDED( context->Map( g_popup_blur.constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) )
		{
			std::memcpy( mapped.pData, &constants, sizeof( constants ) );
			context->Unmap( g_popup_blur.constants, 0 );
			const D3D11_RECT scissor{
				static_cast<LONG>( std::floor( command->ClipRect.x ) ),
				static_cast<LONG>( std::floor( command->ClipRect.y ) ),
				static_cast<LONG>( std::ceil( command->ClipRect.z ) ),
				static_cast<LONG>( std::ceil( command->ClipRect.w ) ) };
			context->RSSetScissorRects( 1, &scissor );
			context->IASetInputLayout( nullptr );
			context->IASetVertexBuffers( 0, 0, nullptr, nullptr, nullptr );
			context->IASetIndexBuffer( nullptr, DXGI_FORMAT_UNKNOWN, 0 );
			context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
			context->VSSetShader( g_popup_blur.vertex_shader, nullptr, 0 );
			context->VSSetConstantBuffers( 0, 1, &g_popup_blur.constants );
			context->PSSetShader( g_popup_blur.pixel_shader, nullptr, 0 );
			context->PSSetConstantBuffers( 0, 1, &g_popup_blur.constants );
			context->PSSetShaderResources( 0, 1, &g_popup_blur.copy_view );
			context->PSSetSamplers( 0, 1, &render_state->SamplerDefault );
			const float blend_factor[ 4 ]{};
			context->OMSetBlendState( nullptr, blend_factor, 0xffffffff );
			context->Draw( 3, 0 );
			ID3D11ShaderResourceView* empty{};
			context->PSSetShaderResources( 0, 1, &empty );
		}
		source->Release( );
		target->Release( );
	}

	ImVec4 to_imvec( const zdraw::rgba& color )
	{
		return { color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f };
	}

	void from_imvec( const ImVec4& value, zdraw::rgba& color )
	{
		color.r = static_cast<std::uint8_t>( std::clamp( value.x, 0.0f, 1.0f ) * 255.0f + 0.5f );
		color.g = static_cast<std::uint8_t>( std::clamp( value.y, 0.0f, 1.0f ) * 255.0f + 0.5f );
		color.b = static_cast<std::uint8_t>( std::clamp( value.z, 0.0f, 1.0f ) * 255.0f + 0.5f );
		color.a = static_cast<std::uint8_t>( std::clamp( value.w, 0.0f, 1.0f ) * 255.0f + 0.5f );
	}

	float approach( float current, float target, float duration )
	{
		const auto step = duration > 0.0f ? ImGui::GetIO( ).DeltaTime / duration : 1.0f;
		return current + ( target - current ) * std::clamp( step, 0.0f, 1.0f );
	}

	ImVec4 mix( const ImVec4& from, const ImVec4& to, float amount )
	{
		return {
			from.x + ( to.x - from.x ) * amount,
			from.y + ( to.y - from.y ) * amount,
			from.z + ( to.z - from.z ) * amount,
			from.w + ( to.w - from.w ) * amount };
	}

	float animate_state( std::unordered_map<ImGuiID, float>& animations, ImGuiID id, bool enabled, float duration = 0.30f )
	{
		auto& value = animations[ id ];
		value = approach( value, enabled ? 1.0f : 0.0f, duration );
		return value;
	}

	float animate_selection( ImGuiID id, bool active )
	{
		auto& value = g_active_animations[ id ];
		if ( !active )
		{
			value = 0.0f;
			return value;
		}

		value = approach( value, 1.0f, 0.10f );
		if ( value > 0.995f ) value = 1.0f;
		return value;
	}

	void spring_to( float& position, float& velocity, float target )
	{
		const auto delta_time = std::min( ImGui::GetIO( ).DeltaTime, 1.0f / 30.0f );
		constexpr auto stiffness = 430.0f;
		constexpr auto damping = 24.0f;
		velocity += ( target - position ) * stiffness * delta_time;
		velocity *= std::exp( -damping * delta_time );
		position += velocity * delta_time;

		if ( std::abs( target - position ) < 0.0005f && std::abs( velocity ) < 0.005f )
		{
			position = target;
			velocity = 0.0f;
		}
	}

	void pointer_cursor_if_hovered( )
	{
		if ( ImGui::IsItemHovered( ) ) ImGui::SetMouseCursor( ImGuiMouseCursor_Hand );
	}

	void soft_shadow( ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding, ImVec2 offset, float spread, ImVec4 tint, float strength )
	{
		constexpr int layers = 18;
		for ( int layer = layers; layer >= 1; --layer )
		{
			const auto outer = static_cast<float>( layer ) / layers;
			const auto expansion = spread * outer;
			const auto falloff = 1.0f - outer;
			auto color = tint;
			color.w = strength * ( 0.012f + falloff * falloff * 0.075f );
			draw->AddRectFilled(
				{ min.x + offset.x - expansion, min.y + offset.y - expansion },
				{ max.x + offset.x + expansion, max.y + offset.y + expansion },
				packed( color ), rounding + expansion );
		}
	}

	void add_vertical_gradient_rounded( ImDrawList* draw, ImVec2 min, ImVec2 max,
		ImU32 top, ImU32 bottom, float rounding, ImDrawFlags corners )
	{
		const auto top_alpha = static_cast<float>( ( top >> IM_COL32_A_SHIFT ) & 0xff );
		if ( top_alpha <= 0.0f )
		{
			return;
		}

		draw->PathRect( min, max, rounding, corners );
		const auto first = draw->VtxBuffer.Size;
		draw->PathFillConvex( top );
		const auto last = draw->VtxBuffer.Size;

		const auto bottom_alpha = static_cast<float>( ( bottom >> IM_COL32_A_SHIFT ) & 0xff );
		const auto span = std::max( 1.0f, max.y - min.y );
		const auto tr = ( top >> IM_COL32_R_SHIFT ) & 0xff;
		const auto tg = ( top >> IM_COL32_G_SHIFT ) & 0xff;
		const auto tb = ( top >> IM_COL32_B_SHIFT ) & 0xff;
		for ( int i = first; i < last; ++i )
		{
			auto& vertex = draw->VtxBuffer[ i ];
			const auto t = std::clamp( ( vertex.pos.y - min.y ) / span, 0.0f, 1.0f );
			const auto desired = top_alpha + ( bottom_alpha - top_alpha ) * t;

			const auto existing = static_cast<float>( ( vertex.col >> IM_COL32_A_SHIFT ) & 0xff );
			const auto alpha = static_cast<int>( std::clamp( existing * ( desired / top_alpha ), 0.0f, 255.0f ) );
			vertex.col = IM_COL32( tr, tg, tb, alpha );
		}
	}

	void draw_popup_surface( ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding )
	{
		popup_blur_request blur{
			max.x - min.x, max.y - min.y, rounding, 2.25f };
		draw->PushClipRect( min, max, true );
		draw->AddCallback( popup_blur_callback, &blur, sizeof( blur ) );
		draw->AddCallback( ImDrawCallback_ResetRenderState, nullptr );
		draw->PopClipRect( );
		draw->AddRectFilled( min, max, packed( k_bg_popup ), rounding );
		draw->AddRectFilled( min + ImVec2{ 1.0f, 1.0f }, max - ImVec2{ 1.0f, 1.0f },
			IM_COL32( 224, 226, 236, 10 ), std::max( 0.0f, rounding - 1.0f ) );
		add_vertical_gradient_rounded( draw, min + ImVec2{ 1.0f, 1.0f },
			max - ImVec2{ 1.0f, 1.0f }, IM_COL32( 255, 255, 255, 8 ),
			IM_COL32( 255, 255, 255, 0 ), std::max( 0.0f, rounding - 1.0f ),
			ImDrawFlags_RoundCornersAll );
		draw->AddRect( min, max, IM_COL32( 255, 255, 255, 24 ), rounding, 0, 1.0f );
	}

	void add_linear_gradient_rounded( ImDrawList* draw, ImVec2 min, ImVec2 max,
		ImU32 from, ImU32 to, float rounding, ImDrawFlags corners, bool horizontal )
	{
		draw->PathRect( min, max, rounding, corners );
		const auto first = draw->VtxBuffer.Size;
		draw->PathFillConvex( IM_COL32_WHITE );
		const auto last = draw->VtxBuffer.Size;
		const auto span = std::max( 1.0f, horizontal ? max.x - min.x : max.y - min.y );
		const auto channel = [ ]( ImU32 value, int shift )
		{
			return static_cast<float>( ( value >> shift ) & 0xff );
		};
		for ( int index = first; index < last; ++index )
		{
			auto& vertex = draw->VtxBuffer[ index ];
			const auto coordinate = horizontal ? vertex.pos.x - min.x : vertex.pos.y - min.y;
			const auto t = std::clamp( coordinate / span, 0.0f, 1.0f );
			const auto coverage = channel( vertex.col, IM_COL32_A_SHIFT ) / 255.0f;
			const auto lerp_channel = [ & ]( int shift )
			{
				return static_cast<int>( std::clamp(
					channel( from, shift ) + ( channel( to, shift ) - channel( from, shift ) ) * t,
					0.0f, 255.0f ) );
			};
			vertex.col = IM_COL32(
				lerp_channel( IM_COL32_R_SHIFT ), lerp_channel( IM_COL32_G_SHIFT ),
				lerp_channel( IM_COL32_B_SHIFT ),
				static_cast<int>( lerp_channel( IM_COL32_A_SHIFT ) * coverage ) );
		}
	}

	void push_menu_font( zdraw::font* font )
	{
		if ( font && font->im_font )
		{
			ImGui::PushFont( font->im_font, font->font_size );
		}
		else
		{
			ImGui::PushFont( nullptr, ImGui::GetStyle( ).FontSizeBase );
		}
	}

	std::size_t utf8_decode( const char* begin, const char* end, unsigned int& out )
	{
		const auto lead = static_cast<unsigned char>( *begin );
		const auto available = static_cast<std::size_t>( end - begin );

		std::size_t length{ 1 };
		unsigned int codepoint{ lead };

		if ( lead >= 0xf0 )      { length = 4; codepoint = lead & 0x07u; }
		else if ( lead >= 0xe0 ) { length = 3; codepoint = lead & 0x0fu; }
		else if ( lead >= 0xc0 ) { length = 2; codepoint = lead & 0x1fu; }

		if ( length > available )
		{
			out = lead;
			return 1;
		}

		for ( std::size_t i = 1; i < length; ++i )
		{
			const auto continuation = static_cast<unsigned char>( begin[ i ] );
			if ( ( continuation & 0xc0 ) != 0x80 )
			{
				out = lead;
				return 1;
			}

			codepoint = ( codepoint << 6 ) | ( continuation & 0x3fu );
		}

		out = codepoint;
		return length;
	}

	void draw_section_title( ImDrawList* draw, ImVec2 position, float line_end_x, std::string_view title, bool uppercase = false )
	{
		const auto* title_wrapper = app::context().overlay.fonts( ).menu_semibold_13;
		auto* title_font = title_wrapper && title_wrapper->im_font ? title_wrapper->im_font : ImGui::GetFont( );
		const auto title_size = title_wrapper && title_wrapper->im_font ? title_wrapper->font_size : ImGui::GetFontSize( );

		auto title_x = position.x;
		for ( std::size_t i = 0; i < title.size( ); )
		{
			unsigned int codepoint{};
			const auto* begin = title.data( ) + i;
			i += utf8_decode( begin, title.data( ) + title.size( ), codepoint );
			const auto* end = title.data( ) + i;

			if ( uppercase )
			{
				if ( codepoint >= 'a' && codepoint <= 'z' ) codepoint -= 0x20;
				else if ( codepoint >= 0x430 && codepoint <= 0x44f ) codepoint -= 0x20;
				else if ( codepoint == 0x451 ) codepoint = 0x401;
			}

			title_font->RenderChar( draw, title_size, { title_x, position.y }, IM_COL32( 255, 255, 255, 217 ), static_cast<ImWchar>( codepoint ) );
			title_x += title_font->CalcTextSizeA( title_size, FLT_MAX, 0.0f, begin, end ).x + 1.5f;
		}

		const auto line_start = ImVec2{ title_x + 10.5f, position.y + title_size * 0.5f };
		if ( line_start.x < line_end_x )
		{
			draw->AddRectFilledMultiColor( line_start, { line_end_x, line_start.y + 1.0f },
				packed( k_border_light ), IM_COL32( 255, 255, 255, 0 ), IM_COL32( 255, 255, 255, 0 ), packed( k_border_light ) );
		}
	}

	void begin_row( const char* label, float control_width )
	{
		ImGui::PushID( label );
		const auto y = ImGui::GetCursorPosY( );
		g_row_start_y_stack.push_back( y );
		ImGui::SetCursorPosY( y + 13.0f );
		const auto* translated = render::localization::tr( label );
		const auto text_min = ImGui::GetCursorScreenPos( );
		const auto label_max_x = ImGui::GetWindowPos( ).x
			+ ImGui::GetWindowContentRegionMax( ).x - control_width - 8.0f;
		if ( label_max_x > text_min.x )
			ImGui::RenderTextEllipsis( ImGui::GetWindowDrawList( ), text_min,
				{ label_max_x, text_min.y + ImGui::GetTextLineHeight( ) },
				label_max_x, translated, nullptr, nullptr );
		ImGui::SetCursorPos( { ImGui::GetWindowContentRegionMax( ).x - control_width, y + 9.0f } );
	}

	void clipped_row_text( const std::string_view text, const ImVec4 color = k_text_muted )
	{
		const auto minimum = ImGui::GetCursorScreenPos( );
		const auto maximum_x = ImGui::GetWindowPos( ).x + ImGui::GetWindowContentRegionMax( ).x;
		const auto available = std::max( 1.0f, maximum_x - minimum.x );
		ImGui::PushStyleColor( ImGuiCol_Text, color );
		ImGui::RenderTextEllipsis( ImGui::GetWindowDrawList( ), minimum,
			{ maximum_x, minimum.y + ImGui::GetTextLineHeight( ) }, maximum_x,
			text.data( ), text.data( ) + text.size( ), nullptr );
		ImGui::PopStyleColor( );
		ImGui::InvisibleButton( "##clipped_row_text", { available, ImGui::GetTextLineHeight( ) } );
		if ( ImGui::IsItemHovered( ) && ImGui::CalcTextSize( text.data( ), text.data( ) + text.size( ) ).x > available )
			ImGui::SetTooltip( "%.*s", static_cast<int>( text.size( ) ), text.data( ) );
	}

	void end_row( )
	{
		if ( g_row_start_y_stack.empty( ) ) return;
		const auto row_start_y = g_row_start_y_stack.back( );
		g_row_start_y_stack.pop_back( );
		ImGui::SetCursorPosY( row_start_y );
		const auto registered_height = std::max( 0.0f, k_row_height - ImGui::GetStyle( ).ItemSpacing.y );
		ImGui::Dummy( { 0.0f, registered_height } );
		ImGui::PopID( );
	}

	void toggle_control( bool& value )
	{
		constexpr auto switch_size = ImVec2{ 46.0f, 24.0f };
		const auto id = ImGui::GetID( "##toggle" );
		if ( ImGui::InvisibleButton( "##toggle", switch_size ) )
		{
			value = !value;
		}

		auto& animation = g_toggle_animations[ id ];
		spring_to( animation.position, animation.velocity, value ? 1.0f : 0.0f );
		animation.reveal = approach( animation.reveal, 1.0f, 0.22f );

		const auto reveal = 1.0f - std::pow( 1.0f - std::clamp( animation.reveal, 0.0f, 1.0f ), 3.0f );
		const auto raw_min = ImGui::GetItemRectMin( );
		const auto raw_max = ImGui::GetItemRectMax( );
		const auto center = ( raw_min + raw_max ) * 0.5f;
		const auto half_size = ImVec2{ switch_size.x * 0.5f * reveal, switch_size.y * 0.5f * ( 0.82f + reveal * 0.18f ) };
		const auto min = center - half_size;
		const auto max = center + half_size;
		const auto hovered = ImGui::IsItemHovered( );
		pointer_cursor_if_hovered( );
		const auto hover = animate_state( g_hover_animations, id, hovered, 0.16f );
		const auto position = std::clamp( animation.position, 0.0f, 1.0f );
		auto* draw = ImGui::GetWindowDrawList( );
		const auto off_track = mix( ImVec4{ 38.0f / 255.0f, 38.0f / 255.0f, 48.0f / 255.0f, 0.88f }, ImVec4{ 50.0f / 255.0f, 50.0f / 255.0f, 62.0f / 255.0f, 0.96f }, hover );
		auto track = mix( off_track, k_accent, position );
		track.w *= reveal;

		draw->AddRectFilled( min, max, packed( track ), 12.0f );

		const auto knob_target_x = raw_min.x + 12.0f + 22.0f * position;
		const auto knob_center = ImVec2{ center.x + ( knob_target_x - center.x ) * reveal, center.y };
		const auto motion = std::min( std::abs( animation.velocity ) * 0.018f, 0.16f );
		const auto knob_radius = ( 9.0f + hover * 0.35f ) * reveal;
		draw->AddCircleFilled( knob_center + ImVec2{ 0.0f, 1.5f }, knob_radius + 1.0f, IM_COL32( 0, 0, 0, static_cast<int>( 55.0f * reveal ) ), 28 );
		draw->AddEllipseFilled( knob_center, { knob_radius * ( 1.0f + motion ), knob_radius * ( 1.0f - motion * 0.45f ) }, IM_COL32( 250, 250, 252, static_cast<int>( 255.0f * reveal ) ), 0.0f, 28 );
		draw->AddCircle( knob_center, knob_radius, IM_COL32( 255, 255, 255, static_cast<int>( 110.0f * reveal ) ), 28, 1.0f );
	}

	void toggle_row( const char* label, bool& value )
	{
		begin_row( label, 46.0f );
		toggle_control( value );
		end_row( );
	}

	void toggle_color_row( const char* label, bool& value, zdraw::rgba& color )
	{
		constexpr auto options_width = 28.0f;
		constexpr auto spacing = 8.0f;
		constexpr auto switch_width = 46.0f;
		begin_row( label, options_width + spacing + switch_width );

		const auto picker_id = ImGui::GetID( "##picker" );
		ImGui::InvisibleButton( "##color_options", { options_width, 24.0f } );
		const auto options_min = ImGui::GetItemRectMin( );
		const auto options_max = ImGui::GetItemRectMax( );
		pointer_cursor_if_hovered( );
		if ( ImGui::IsItemClicked( ) ) ImGui::OpenPopup( "##picker" );

		auto* draw = ImGui::GetWindowDrawList( );
		const auto dot_y = ( options_min.y + options_max.y ) * 0.5f;
		for ( int dot = 0; dot < 3; ++dot )
		{
			draw->AddCircleFilled( { options_min.x + 5.0f + dot * 9.0f, dot_y }, 1.08f, IM_COL32_WHITE, 12 );
		}

		ImGui::SetCursorScreenPos( { options_max.x + spacing, options_min.y } );
		toggle_control( value );
		color_picker_popup( color, options_min, options_max, picker_id );
		end_row( );
	}

	template<typename value_t>
	void slider_row_impl( const char* label, value_t& value, value_t minimum, value_t maximum, const char* suffix, value_t step )
	{
		constexpr auto control_width = 130.0f;
		begin_row( label, control_width );
		const auto id = ImGui::GetID( "##slider" );
		const auto range = static_cast<double>( maximum ) - static_cast<double>( minimum );
		const auto quantum = std::max( static_cast<double>( step ), std::numeric_limits<double>::epsilon( ) );
		const auto decimal_places = [ ]( double increment )
		{
			if ( increment >= 1.0 ) return 0;
			auto scaled = increment;
			for ( int digits = 1; digits <= 4; ++digits )
			{
				scaled *= 10.0;
				if ( std::abs( scaled - std::round( scaled ) ) < 0.00001 ) return digits;
			}
			return 4;
		};
		const auto digits = std::is_integral_v<value_t> ? 0 : decimal_places( quantum );
		const auto format_value = [ & ]( char* output, std::size_t size, bool include_suffix )
		{
			if constexpr ( std::is_integral_v<value_t> )
				std::snprintf( output, size, include_suffix ? "%d%s" : "%d",
					static_cast<int>( value ), suffix );
			else
				std::snprintf( output, size, include_suffix ? "%.*f%s" : "%.*f",
					digits, static_cast<double>( value ), suffix );
		};
		const auto assign = [ & ]( double raw )
		{
			raw = static_cast<double>( minimum ) +
				std::round( ( raw - static_cast<double>( minimum ) ) / quantum ) * quantum;
			value = static_cast<value_t>( std::clamp( raw,
				static_cast<double>( minimum ), static_cast<double>( maximum ) ) );
		};

		if ( g_slider_edit.id && g_slider_edit.last_seen_frame < GImGui->FrameCount - 1 )
			g_slider_edit = {};

		char value_text[ 48 ]{};
		format_value( value_text, sizeof( value_text ), true );
		const auto text_size = ImGui::CalcTextSize( value_text );
		ImGui::InvisibleButton( "##slider", { control_width, 28.0f } );
		pointer_cursor_if_hovered( );
		const auto item_min = ImGui::GetItemRectMin( );
		const auto item_max = ImGui::GetItemRectMax( );
		const auto actual_width = std::max( 1.0f, item_max.x - item_min.x );
		const auto track_a = ImVec2{ item_min.x + 7.0f, item_min.y + 22.0f };
		const auto track_b = ImVec2{ item_max.x - 7.0f, item_min.y + 26.0f };
		const auto track_width = std::max( 1.0f, track_b.x - track_a.x );
		const auto value_rect = ImRect{
			{ item_max.x - text_size.x - 5.0f, item_min.y - 3.0f },
			{ item_max.x + 3.0f, item_min.y + text_size.y + 4.0f } };
		const auto value_hovered = value_rect.Contains( ImGui::GetIO( ).MousePos );
		const auto active = ImGui::IsItemActive( );
		auto& drag = g_slider_drags[ id ];

		if ( value_hovered && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
		{
			g_slider_edit = {};
			g_slider_edit.id = id;
			g_slider_edit.request_focus = true;
			g_slider_edit.last_seen_frame = GImGui->FrameCount;
			format_value( g_slider_edit.text.data( ), g_slider_edit.text.size( ), false );
			drag = {};
			ImGui::ClearActiveID( );
		}

		const auto editing = g_slider_edit.id == id;
		if ( active && ImGui::IsItemActivated( ) )
		{
			drag.pressed_on_value = value_hovered;
			drag.dragging = !drag.pressed_on_value && !editing;
			drag.pixel_remainder = 0.0;
			if ( drag.dragging )
			{

				const auto fraction = std::clamp(
					( ImGui::GetIO( ).MousePos.x - track_a.x ) / track_width, 0.0f, 1.0f );
				assign( static_cast<double>( minimum ) + fraction * range );
			}
		}
		else if ( active && drag.dragging && !editing )
		{
			const auto intervals = range > 0.0
				? std::max( 1.0, std::round( range / quantum ) ) : 1.0;
			const auto pixels_per_step = std::max(
				1.0, static_cast<double>( track_width ) / intervals );
			drag.pixel_remainder += static_cast<double>( ImGui::GetIO( ).MouseDelta.x );
			const auto steps = std::trunc( drag.pixel_remainder / pixels_per_step );
			if ( steps != 0.0 )
			{
				assign( static_cast<double>( value ) + steps * quantum );
				drag.pixel_remainder -= steps * pixels_per_step;
			}
		}
		else if ( !active )
		{
			drag = {};
		}

		const auto target = range > 0.0
			? static_cast<float>( ( static_cast<double>( value ) - static_cast<double>( minimum ) ) / range )
			: 0.0f;
		auto& shown = g_slider_animations[ id ];
		if ( shown == 0.0f && target > 0.0f )
		{
			shown = target;
		}

		shown = active && drag.dragging ? target : approach( shown, target, 0.10f );

		auto* draw = ImGui::GetWindowDrawList( );
		if ( !editing )
			draw->AddText( { item_max.x - text_size.x, item_min.y }, packed( k_text_muted ), value_text );
		draw->AddRectFilled( track_a, track_b, IM_COL32( 255, 255, 255, 26 ), 2.0f );
		const auto x = track_a.x + track_width * shown;
		draw->AddRectFilled( track_a, { x, track_b.y }, packed( k_accent ), 2.0f );
		const auto radius = ImGui::IsItemHovered( ) || ( active && drag.dragging ) ? 8.5f : 7.0f;
		draw->AddCircleFilled( { x, item_min.y + 24.0f }, radius, IM_COL32_WHITE );
		draw->AddCircle( { x, item_min.y + 24.0f }, radius, IM_COL32( 0, 0, 0, 100 ) );

		if ( editing )
		{
			g_slider_edit.last_seen_frame = GImGui->FrameCount;
			const auto editor_width = std::clamp( text_size.x + 18.0f, 52.0f, actual_width );
			ImGui::SetCursorScreenPos( { item_max.x - editor_width, item_min.y - 3.0f } );
			ImGui::PushStyleColor( ImGuiCol_FrameBg, k_bg_panel );
			ImGui::PushStyleColor( ImGuiCol_FrameBgHovered, k_bg_hover );
			ImGui::PushStyleColor( ImGuiCol_FrameBgActive, k_bg_hover );
			ImGui::PushStyleColor( ImGuiCol_Border, k_border_light );
			ImGui::PushStyleColor( ImGuiCol_Text, k_text_main );
			ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 5.0f );
			ImGui::PushStyleVar( ImGuiStyleVar_FrameBorderSize, 1.0f );
			ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, { 6.0f, 2.0f } );
			ImGui::SetNextItemWidth( editor_width );
			if ( g_slider_edit.request_focus )
			{
				ImGui::SetKeyboardFocusHere( );
				g_slider_edit.request_focus = false;
			}
			const auto enter = ImGui::InputText( "##slider_value_input",
				g_slider_edit.text.data( ), g_slider_edit.text.size( ),
				ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue );
			const auto escape = ImGui::IsKeyPressed( ImGuiKey_Escape, false );
			const auto deactivated = ImGui::IsItemDeactivated( );
			ImGui::PopStyleVar( 3 );
			ImGui::PopStyleColor( 5 );

			if ( escape )
			{
				g_slider_edit = {};
			}
			else if ( enter || deactivated )
			{
				auto normalized = std::string{ g_slider_edit.text.data( ) };
				std::ranges::replace( normalized, ',', '.' );
				char* parsed_end{};
				const auto parsed = std::strtod( normalized.c_str( ), &parsed_end );
				while ( parsed_end && *parsed_end &&
					std::isspace( static_cast<unsigned char>( *parsed_end ) ) ) ++parsed_end;
				if ( parsed_end != normalized.c_str( ) && parsed_end &&
					*parsed_end == '\0' && std::isfinite( parsed ) )
				{
					assign( parsed );
				}
				g_slider_edit = {};
			}
		}

		end_row( );
	}

	void slider_row( const char* label, int& value, int minimum, int maximum, const char* suffix = "" )
	{
		slider_row_impl( label, value, minimum, maximum, suffix, 1 );
	}

	void slider_row( const char* label, float& value, float minimum, float maximum, const char* suffix, float step )
	{
		slider_row_impl( label, value, minimum, maximum, suffix, step );
	}

	void slider_percent_row( const char* label, float& value )
	{
		auto percent = std::clamp( value, 0.0f, 1.0f ) * 100.0f;
		slider_row_impl( label, percent, 0.0f, 100.0f, "%", 1.0f );
		value = percent * 0.01f;
	}

	void draw_checkmark( ImDrawList* draw, ImVec2 center, ImU32 color, float amount, float thickness = 1.7f )
	{
		if ( amount <= 0.01f ) return;

		const auto a = ImVec2{ center.x - 4.0f, center.y };
		const auto b = ImVec2{ center.x - 1.0f, center.y + 3.0f };
		const auto c = ImVec2{ center.x + 5.0f, center.y - 4.0f };
		if ( amount < 0.42f )
		{
			const auto t = amount / 0.42f;
			draw->AddLine( a, a + ( b - a ) * t, color, thickness );
			return;
		}

		draw->AddLine( a, b, color, thickness );
		const auto t = ( amount - 0.42f ) / 0.58f;
		draw->AddLine( b, b + ( c - b ) * std::clamp( t, 0.0f, 1.0f ), color, thickness );
	}

	void draw_dropdown_chevron( ImDrawList* draw, ImVec2 center, ImU32 color, float open_amount )
	{
		const auto lift = 2.0f - 4.0f * open_amount;
		const auto left = ImVec2{ center.x - 4.0f, center.y - lift };
		const auto middle = ImVec2{ center.x, center.y + lift };
		const auto right = ImVec2{ center.x + 4.0f, center.y - lift };
		draw->AddLine( left, middle, color, 1.55f );
		draw->AddLine( middle, right, color, 1.55f );
	}

	void select_row( const char* label, int& value, std::span<const char* const> options )
	{
		if ( options.empty( ) ) return;

		constexpr auto control_width = 140.0f;
		constexpr auto control_height = 30.0f;
		constexpr auto option_height = 34.0f;

		auto popup_width = control_width;
		for ( const auto* option : options )
		{
			popup_width = std::max( popup_width, ImGui::CalcTextSize( render::localization::tr( option ) ).x + 48.0f );
		}

		begin_row( label, control_width );
		const auto index = std::clamp( value, 0, static_cast<int>( options.size( ) ) - 1 );
		const auto button_id = ImGui::GetID( "##select_button" );
		ImGui::InvisibleButton( "##select_button", { control_width, control_height } );
		if ( ImGui::IsItemClicked( ) )
		{
			ImGui::OpenPopup( "##options" );
		}
		pointer_cursor_if_hovered( );
		const auto button_hovered = ImGui::IsItemHovered( );
		const auto popup_open = ImGui::IsPopupOpen( "##options" );
		const auto hover_t = animate_state( g_hover_animations, button_id, button_hovered, 0.14f );
		const auto open_t = animate_state( g_active_animations, button_id, popup_open, 0.16f );
		const auto button_min = ImGui::GetItemRectMin( );
		const auto button_max = ImGui::GetItemRectMax( );

		auto* button_draw = ImGui::GetWindowDrawList( );
		const auto control_bg = mix(
			ImVec4{ 27.0f / 255.0f, 27.0f / 255.0f, 35.0f / 255.0f, 0.94f },
			ImVec4{ 38.0f / 255.0f, 36.0f / 255.0f, 48.0f / 255.0f, 0.98f },
			std::max( hover_t, open_t ) );
		button_draw->AddRectFilled( button_min, button_max, packed( control_bg ), 9.0f );
		auto control_border = mix( k_border, k_border_light, hover_t );
		control_border = mix( control_border, k_accent, open_t * 0.65f );
		control_border.w = 0.08f + hover_t * 0.08f + open_t * 0.20f;
		button_draw->AddRect( button_min, button_max, packed( control_border ), 9.0f, 0, 1.0f );
		const auto* selected_text = render::localization::tr( options[ index ] );
		const auto label_size = ImGui::CalcTextSize( selected_text );

		button_draw->PushClipRect( button_min, { button_max.x - 28.0f, button_max.y }, true );
		button_draw->AddText( { button_min.x + 12.0f, button_min.y + ( control_height - label_size.y ) * 0.5f }, packed( k_text_main ), selected_text );
		button_draw->PopClipRect( );

		draw_dropdown_chevron( button_draw,
			{ button_max.x - 15.0f, button_min.y + control_height * 0.5f },
			packed( mix( k_text_muted, k_text_main, std::max( hover_t, open_t ) ) ), open_t );

		const auto popup_height = 8.0f + options.size( ) * option_height;
		auto popup_position = ImVec2{ button_min.x, button_max.y + 4.0f };
		if ( popup_position.y + popup_height > settings_bounds_max( ).y - 8.0f ) popup_position.y = button_min.y - popup_height - 4.0f;

		popup_position.x = std::max( button_max.x - popup_width, settings_bounds_min( ).x + 8.0f );
		ImGui::SetNextWindowPos( popup_position, ImGuiCond_Always );
		ImGui::SetNextWindowSize( { popup_width, popup_height }, ImGuiCond_Always );
		ImGui::PushStyleColor( ImGuiCol_PopupBg, ImVec4{ 0, 0, 0, 0 } );
		ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, { 0.0f, 0.0f } );
		ImGui::PushStyleVar( ImGuiStyleVar_PopupRounding, 8.0f );
		ImGui::PushStyleVar( ImGuiStyleVar_PopupBorderSize, 0.0f );
		ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, { 0.0f, 0.0f } );
		if ( ImGui::BeginPopup( "##options", ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground ) )
		{
			const auto popup_min = ImGui::GetWindowPos( );
			const auto popup_max = ImVec2{ popup_min.x + ImGui::GetWindowSize( ).x, popup_min.y + ImGui::GetWindowSize( ).y };
			auto* popup_draw = ImGui::GetWindowDrawList( );

			popup_draw->PushClipRect( settings_bounds_min( ), settings_bounds_max( ), false );
			soft_shadow( popup_draw, popup_min, popup_max, 10.0f, { 0.0f, 6.0f },
				12.0f, { 0.0f, 0.0f, 0.0f, 1.0f }, 0.72f );
			draw_popup_surface( popup_draw, popup_min, popup_max, 10.0f );
			popup_draw->PopClipRect( );
			for ( int i = 0; i < static_cast<int>( options.size( ) ); ++i )
			{
				ImGui::PushID( i );
				ImGui::SetCursorScreenPos( popup_min + ImVec2{ 4.0f, 4.0f + option_height * i } );
				const auto option_id = ImGui::GetID( "##option" );
				ImGui::InvisibleButton( "##option", { popup_width - 8.0f, option_height } );
				const auto option_hovered = ImGui::IsItemHovered( );
				pointer_cursor_if_hovered( );
				const auto option_min = ImGui::GetItemRectMin( );
				const auto option_max = ImGui::GetItemRectMax( );
				const auto selected = value == i;
				const auto option_hover_t = animate_state( g_hover_animations, option_id, option_hovered, 0.11f );
				const auto selected_t = animate_state( g_active_animations, option_id, selected, 0.14f );
				auto option_bg = mix( ImVec4{ 1, 1, 1, 0 }, k_bg_hover, option_hover_t );
				option_bg = mix( option_bg, ImVec4{ k_accent.x, k_accent.y, k_accent.z, 0.13f }, selected_t );
				const auto option_visual_min = option_min + ImVec2{ 0.0f, 2.0f };
				const auto option_visual_max = option_max - ImVec2{ 0.0f, 2.0f };
				popup_draw->AddRectFilled( option_visual_min, option_visual_max, packed( option_bg ), 7.0f );
				if ( selected_t > 0.01f )
				{
					popup_draw->AddRectFilled(
						{ option_min.x + 3.0f, option_visual_min.y + 6.0f },
						{ option_min.x + 5.0f, option_visual_max.y - 6.0f },
						packed( ImVec4{ k_accent.x, k_accent.y, k_accent.z, selected_t } ), 1.0f );
				}
				popup_draw->AddText(
					{ option_min.x + 12.0f + option_hover_t * 2.0f,
						option_min.y + ( option_height - ImGui::GetTextLineHeight( ) ) * 0.5f },
					packed( mix( k_text_muted, k_text_main, std::max( option_hover_t, selected_t ) ) ),
					render::localization::tr( options[ i ] ) );
				draw_checkmark( popup_draw,
					{ option_max.x - 16.0f, option_min.y + option_height * 0.5f },
					packed( ImVec4{ k_accent.x, k_accent.y, k_accent.z, selected_t } ), selected_t );
				if ( ImGui::IsItemClicked( ) )
				{
					value = i;
					ImGui::CloseCurrentPopup( );
				}
				ImGui::PopID( );
			}
			ImGui::EndPopup( );
		}
		ImGui::PopStyleVar( 4 );
		ImGui::PopStyleColor( );
		end_row( );
	}

	void multiselect_row( const char* label, int& mask, std::span<const std::pair<const char*, int>> options, int all_mask )
	{
		if ( options.empty( ) ) return;

		constexpr auto control_width = 140.0f;
		constexpr auto control_height = 30.0f;
		constexpr auto option_height = 34.0f;
		begin_row( label, control_width );

		std::string summary{};
		if ( ( mask & all_mask ) == all_mask ) summary = render::localization::tr( "All" );
		else
		{
			for ( const auto& [name, bit] : options )
				if ( mask & bit ) summary += ( summary.empty( ) ? "" : ", " ) + std::string( render::localization::tr( name ) );
			if ( summary.empty( ) ) summary = render::localization::tr( "None" );
		}

		const auto button_id = ImGui::GetID( "##multiselect_button" );
		ImGui::InvisibleButton( "##multiselect_button", { control_width, control_height } );
		if ( ImGui::IsItemClicked( ) ) ImGui::OpenPopup( "##multi_options" );
		pointer_cursor_if_hovered( );
		const auto button_hovered = ImGui::IsItemHovered( );
		const auto popup_open = ImGui::IsPopupOpen( "##multi_options" );
		const auto hover_t = animate_state( g_hover_animations, button_id, button_hovered, 0.14f );
		const auto open_t = animate_state( g_active_animations, button_id, popup_open, 0.16f );
		const auto button_min = ImGui::GetItemRectMin( );
		const auto button_max = ImGui::GetItemRectMax( );

		auto* button_draw = ImGui::GetWindowDrawList( );
		const auto control_bg = mix(
			ImVec4{ 27.0f / 255.0f, 27.0f / 255.0f, 35.0f / 255.0f, 0.94f },
			ImVec4{ 38.0f / 255.0f, 36.0f / 255.0f, 48.0f / 255.0f, 0.98f },
			std::max( hover_t, open_t ) );
		button_draw->AddRectFilled( button_min, button_max, packed( control_bg ), 9.0f );
		auto control_border = mix( k_border, k_border_light, hover_t );
		control_border = mix( control_border, k_accent, open_t * 0.65f );
		control_border.w = 0.08f + hover_t * 0.08f + open_t * 0.20f;
		button_draw->AddRect( button_min, button_max, packed( control_border ), 9.0f );
		const auto label_size = ImGui::CalcTextSize( summary.c_str( ) );
		button_draw->PushClipRect( button_min, { button_max.x - 28.0f, button_max.y }, true );
		button_draw->AddText( { button_min.x + 12.0f, button_min.y + ( control_height - label_size.y ) * 0.5f }, packed( k_text_main ), summary.c_str( ) );
		button_draw->PopClipRect( );

		draw_dropdown_chevron( button_draw,
			{ button_max.x - 15.0f, button_min.y + control_height * 0.5f },
			packed( mix( k_text_muted, k_text_main, std::max( hover_t, open_t ) ) ), open_t );

		auto popup_width = control_width;
		for ( const auto& [ name, bit ] : options )
		{
			(void)bit;
			popup_width = std::max( popup_width, ImGui::CalcTextSize( render::localization::tr( name ) ).x + 58.0f );
		}
		const auto popup_height = 8.0f + options.size( ) * option_height;
		auto popup_position = ImVec2{ button_min.x, button_max.y + 4.0f };
		if ( popup_position.y + popup_height > settings_bounds_max( ).y - 8.0f ) popup_position.y = button_min.y - popup_height - 4.0f;
		popup_position.x = std::max( button_max.x - popup_width, settings_bounds_min( ).x + 8.0f );
		ImGui::SetNextWindowPos( popup_position, ImGuiCond_Always );
		ImGui::SetNextWindowSize( { popup_width, popup_height }, ImGuiCond_Always );
		ImGui::PushStyleColor( ImGuiCol_PopupBg, ImVec4{ 0, 0, 0, 0 } );
		ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, { 0.0f, 0.0f } );
		ImGui::PushStyleVar( ImGuiStyleVar_PopupRounding, 8.0f );
		ImGui::PushStyleVar( ImGuiStyleVar_PopupBorderSize, 0.0f );
		ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, { 0.0f, 0.0f } );
		if ( ImGui::BeginPopup( "##multi_options", ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground ) )
		{
			const auto popup_min = ImGui::GetWindowPos( );
			const auto popup_max = ImVec2{ popup_min.x + ImGui::GetWindowSize( ).x, popup_min.y + ImGui::GetWindowSize( ).y };
			auto* popup_draw = ImGui::GetWindowDrawList( );
			popup_draw->PushClipRect( settings_bounds_min( ), settings_bounds_max( ), false );
			soft_shadow( popup_draw, popup_min, popup_max, 10.0f, { 0.0f, 6.0f },
				12.0f, { 0.0f, 0.0f, 0.0f, 1.0f }, 0.72f );
			draw_popup_surface( popup_draw, popup_min, popup_max, 10.0f );
			popup_draw->PopClipRect( );
			for ( int i = 0; i < static_cast<int>( options.size( ) ); ++i )
			{
				ImGui::PushID( i );
				ImGui::SetCursorScreenPos( popup_min + ImVec2{ 4.0f, 4.0f + option_height * i } );
				const auto option_id = ImGui::GetID( "##multi_option" );
				ImGui::InvisibleButton( "##multi_option", { popup_width - 8.0f, option_height } );
				const auto option_hovered = ImGui::IsItemHovered( );
				pointer_cursor_if_hovered( );
				const auto option_min = ImGui::GetItemRectMin( );
				const auto option_max = ImGui::GetItemRectMax( );
				const auto bit = options[ i ].second;
				const auto selected = ( mask & bit ) != 0;
				const auto option_hover_t = animate_state( g_hover_animations, option_id, option_hovered, 0.11f );
				const auto selected_t = animate_state( g_active_animations, option_id, selected, 0.14f );
				auto option_bg = mix( ImVec4{ 1, 1, 1, 0 }, k_bg_hover, option_hover_t );
				option_bg = mix( option_bg, ImVec4{ k_accent.x, k_accent.y, k_accent.z, 0.11f }, selected_t );
				const auto option_visual_min = option_min + ImVec2{ 0.0f, 2.0f };
				const auto option_visual_max = option_max - ImVec2{ 0.0f, 2.0f };
				popup_draw->AddRectFilled( option_visual_min, option_visual_max, packed( option_bg ), 7.0f );

				const auto check_min = ImVec2{ option_min.x + 10.0f, option_min.y + 9.0f };
				const auto check_max = check_min + ImVec2{ 16.0f, 16.0f };
				const auto check_bg = mix(
					ImVec4{ 1.0f, 1.0f, 1.0f, 0.035f },
					ImVec4{ k_accent.x, k_accent.y, k_accent.z, 1.0f }, selected_t );
				popup_draw->AddRectFilled( check_min, check_max, packed( check_bg ), 5.0f );
				popup_draw->AddRect( check_min, check_max,
					packed( mix( k_border_light, k_accent, selected_t ) ), 5.0f );
				draw_checkmark( popup_draw, ( check_min + check_max ) * 0.5f,
					IM_COL32( 255, 255, 255, static_cast<int>( selected_t * 255.0f ) ),
					selected_t, 1.8f );

				popup_draw->AddText(
					{ option_min.x + 36.0f + option_hover_t * 2.0f,
						option_min.y + ( option_height - ImGui::GetTextLineHeight( ) ) * 0.5f },
					packed( mix( k_text_muted, k_text_main, std::max( option_hover_t, selected_t ) ) ),
					render::localization::tr( options[ i ].first ) );
				if ( ImGui::IsItemClicked( ) ) mask ^= bit;
				ImGui::PopID( );
			}
			ImGui::EndPopup( );
		}
		ImGui::PopStyleVar( 4 );
		ImGui::PopStyleColor( );
		end_row( );
	}

	void aim_parts_row( int& mask )
	{
		static constexpr std::pair<const char*, int> opts[]{
			{ "Head", config::combat_profile::aim_part::head },
			{ "Body", config::combat_profile::aim_part::body },
			{ "Arms", config::combat_profile::aim_part::arms },
			{ "Legs", config::combat_profile::aim_part::legs },
		};
		multiselect_row( "Hitboxes", mask, opts, config::combat_profile::aim_part::all );
	}

	enum class row_action_icon
	{
		none,
		copy,
		check,
		save,
		folder
	};

	void draw_action_icon( ImDrawList* draw, row_action_icon icon, ImVec2 center, ImU32 color )
	{
		switch ( icon )
		{
		case row_action_icon::copy:
			draw->AddRect( center + ImVec2{ -5.0f, -6.0f }, center + ImVec2{ 4.0f, 4.0f },
				color, 2.0f, 0, 1.35f );
			draw->AddRect( center + ImVec2{ -2.0f, -3.0f }, center + ImVec2{ 7.0f, 7.0f },
				color, 2.0f, 0, 1.35f );
			break;
		case row_action_icon::check:
			draw_checkmark( draw, center, color, 1.0f, 1.8f );
			break;
		case row_action_icon::save:
			draw->AddLine( center + ImVec2{ 0.0f, -6.0f }, center + ImVec2{ 0.0f, 3.0f },
				color, 1.5f );
			draw->AddLine( center + ImVec2{ -3.5f, -0.5f }, center + ImVec2{ 0.0f, 3.0f },
				color, 1.5f );
			draw->AddLine( center + ImVec2{ 3.5f, -0.5f }, center + ImVec2{ 0.0f, 3.0f },
				color, 1.5f );
			draw->AddLine( center + ImVec2{ -5.0f, 6.0f }, center + ImVec2{ 5.0f, 6.0f },
				color, 1.5f );
			break;
		case row_action_icon::folder:
			draw->AddRect( center + ImVec2{ -7.0f, -3.5f }, center + ImVec2{ 7.0f, 6.0f },
				color, 2.0f, 0, 1.45f );
			draw->AddLine( center + ImVec2{ -6.0f, -3.5f }, center + ImVec2{ -3.0f, -6.0f },
				color, 1.45f );
			draw->AddLine( center + ImVec2{ -3.0f, -6.0f }, center + ImVec2{ 1.0f, -6.0f },
				color, 1.45f );
			draw->AddLine( center + ImVec2{ 1.0f, -6.0f }, center + ImVec2{ 3.0f, -3.5f },
				color, 1.45f );
			break;
		default:
			break;
		}
	}

	bool button_row( const char* label, const char* text,
		row_action_icon icon = row_action_icon::none )
	{
		constexpr auto control_width = 140.0f;
		constexpr auto control_height = 30.0f;
		begin_row( label, control_width );

		const auto id = ImGui::GetID( "##button_row" );
		ImGui::InvisibleButton( "##button_row", { control_width, control_height } );
		const auto clicked = ImGui::IsItemClicked( );
		pointer_cursor_if_hovered( );
		const auto hovered = ImGui::IsItemHovered( );
		const auto held = ImGui::IsItemActive( );
		const auto hover_t = animate_state( g_hover_animations, id, hovered, 0.13f );
		const auto held_t = animate_state( g_active_animations, id, held, 0.08f );
		const auto bmin = ImGui::GetItemRectMin( );
		const auto bmax = ImGui::GetItemRectMax( );

		auto* dl = ImGui::GetWindowDrawList( );
		constexpr auto rounding = 9.0f;
		if ( hover_t > 0.01f )
		{
			soft_shadow( dl, bmin, bmax, rounding, { 0.0f, 3.0f }, 7.0f,
				{ k_accent.x, k_accent.y, k_accent.z, 1.0f }, hover_t * 0.24f );
		}
		auto bg = mix(
			ImVec4{ 27.0f / 255.0f, 27.0f / 255.0f, 35.0f / 255.0f, 0.88f },
			ImVec4{ k_accent.x, k_accent.y, k_accent.z, 0.78f },
			hover_t * 0.72f );
		bg = mix( bg, ImVec4{ k_accent.x * 0.72f, k_accent.y * 0.72f,
			k_accent.z * 0.72f, 0.92f }, held_t );
		dl->AddRectFilled( bmin, bmax, packed( bg ), rounding );
		auto border = mix( k_border, ImVec4{ k_accent.x, k_accent.y, k_accent.z, 0.55f }, hover_t );
		border.w += held_t * 0.18f;
		dl->AddRect( bmin, bmax, packed( border ), rounding, 0, 1.0f );

		const auto* button_text = render::localization::tr( text );
		const auto ts = ImGui::CalcTextSize( button_text );
		const auto has_icon = icon != row_action_icon::none;
		const auto content_width = ts.x + ( has_icon ? 21.0f : 0.0f );
		const auto content_x = bmin.x + ( control_width - content_width ) * 0.5f;
		const auto press_offset = ImVec2{ 0.0f, held_t * 1.0f };
		const auto content_color = packed( mix( k_text_muted, k_text_main,
			std::max( hover_t, held_t ) ) );
		if ( has_icon )
		{
			const auto icon_center =
				ImVec2{ content_x + 7.0f, bmin.y + control_height * 0.5f } + press_offset;
			draw_action_icon( dl, icon, icon_center, content_color );
		}
		const auto text_position = ImVec2{
			content_x + ( has_icon ? 21.0f : 0.0f ),
			bmin.y + ( control_height - ts.y ) * 0.5f } + press_offset;
		dl->AddText( text_position, content_color, button_text );

		end_row( );
		return clicked;
	}

	int filter_filename_char( ImGuiInputTextCallbackData* data )
	{
		const auto c = data->EventChar;
		if ( c < 32 || c == '\\' || c == '/' || c == ':' || c == '*' ||
			c == '?' || c == '"' || c == '<' || c == '>' || c == '|' )
			return 1;
		return 0;
	}

	void text_input_row( const char* label, char* buffer, std::size_t size )
	{
		constexpr auto control_width = 190.0f;
		begin_row( label, control_width );
		ImGui::PushStyleColor( ImGuiCol_FrameBg, k_bg_panel );
		ImGui::PushStyleColor( ImGuiCol_FrameBgHovered, k_bg_hover );
		ImGui::PushStyleColor( ImGuiCol_FrameBgActive, k_bg_hover );
		ImGui::PushStyleColor( ImGuiCol_Border, k_border_light );
		ImGui::PushStyleColor( ImGuiCol_Text, k_text_main );
		ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 8.0f );
		ImGui::PushStyleVar( ImGuiStyleVar_FrameBorderSize, 1.0f );
		ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, { 10.0f, 6.0f } );
		ImGui::SetNextItemWidth( control_width );
		ImGui::InputText( "##text_input", buffer, size,
			ImGuiInputTextFlags_CallbackCharFilter, filter_filename_char );
		ImGui::PopStyleVar( 3 );
		ImGui::PopStyleColor( 5 );
		end_row( );
	}

	std::string key_name( int key )
	{
		switch ( key )
		{
		case 0: return render::localization::tr( "None" );
		case VK_LBUTTON: return "M1";
		case VK_RBUTTON: return "M2";
		case VK_MBUTTON: return "M3";
		case VK_XBUTTON1: return "M4";
		case VK_XBUTTON2: return "M5";
		case VK_SHIFT: case VK_LSHIFT: return "LEFT SHIFT";
		case VK_RSHIFT: return "RIGHT SHIFT";
		case VK_CONTROL: case VK_LCONTROL: return "LEFT CTRL";
		case VK_RCONTROL: return "RIGHT CTRL";
		case VK_MENU: case VK_LMENU: return "LEFT ALT";
		case VK_RMENU: return "RIGHT ALT";
		case VK_SPACE: return "SPACE";
		case VK_INSERT: return "INSERT";
		case VK_DELETE: return "DELETE";
		case VK_HOME: return "HOME";
		case VK_END: return "END";
		case VK_PRIOR: return "PAGE UP";
		case VK_NEXT: return "PAGE DOWN";
		case VK_TAB: return "TAB";
		case VK_RETURN: return "ENTER";
		case VK_BACK: return "BACKSPACE";
		case VK_CAPITAL: return "CAPS LOCK";
		case VK_NUMLOCK: return "NUM LOCK";
		case VK_SCROLL: return "SCROLL LOCK";
		case VK_PAUSE: return "PAUSE";
		case VK_UP: return "UP";
		case VK_DOWN: return "DOWN";
		case VK_LEFT: return "LEFT";
		case VK_RIGHT: return "RIGHT";
		default:
			if ( key >= '0' && key <= '9' ) return std::string( 1, static_cast<char>( key ) );
			if ( key >= 'A' && key <= 'Z' ) return std::string( 1, static_cast<char>( key ) );
			if ( key >= VK_F1 && key <= VK_F24 )
				return std::format( "F{}", key - VK_F1 + 1 );
			if ( key >= VK_NUMPAD0 && key <= VK_NUMPAD9 )
				return std::format( "NUM {}", key - VK_NUMPAD0 );
			wchar_t wide_name[64]{};
			const auto scan = ::MapVirtualKeyW( static_cast<UINT>( key ), MAPVK_VK_TO_VSC );
			if ( scan && ::GetKeyNameTextW( static_cast<LONG>( scan << 16 ),
				wide_name, static_cast<int>( std::size( wide_name ) ) ) > 0 )
			{
				char utf8[128]{};
				const auto length = ::WideCharToMultiByte( CP_UTF8, 0, wide_name, -1,
					utf8, static_cast<int>( std::size( utf8 ) ), nullptr, nullptr );
				if ( length > 1 ) return std::string( utf8, length - 1 );
			}
			return std::format( "KEY {}", key );
		}
	}

	[[nodiscard]] bool bind_input_is_down( )
	{
		for ( int button = 0; button < 5; ++button )
		{
			if ( ImGui::IsMouseDown( button ) )
				return true;
		}
		for ( int key = 1; key < 256; ++key )
		{
			const auto imgui_key = overlay_input::key_from_virtual_key( key );
			if ( imgui_key != ImGuiKey_None && ImGui::IsKeyDown( imgui_key ) )
				return true;
		}
		return false;
	}

	[[nodiscard]] int pressed_bind_key( )
	{
		constexpr int mouse_keys[]{
			VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2 };
		for ( int button = 0; button < static_cast<int>( std::size( mouse_keys ) ); ++button )
		{
			if ( ImGui::IsMouseClicked( button, false ) )
				return mouse_keys[ button ];
		}

		for ( int key = 1; key < 256; ++key )
		{
			const auto imgui_key = overlay_input::key_from_virtual_key( key );
			if ( imgui_key != ImGuiKey_None && ImGui::IsKeyPressed( imgui_key, false ) )
				return key;
		}
		return 0;
	}

	void keybind_row( const char* label, int& value )
	{
		begin_row( label, 130.0f );
		const auto listening = g_listening_key == &value;
		const auto pulse = listening ? 0.8f + std::sin( static_cast<float>( ImGui::GetTime( ) ) * 6.28318f ) * 0.2f : 1.0f;
		ImGui::PushStyleColor( ImGuiCol_Button, listening ? ImVec4{ k_accent.x, k_accent.y, k_accent.z, pulse } : k_bg_panel );
		ImGui::PushStyleColor( ImGuiCol_ButtonHovered, listening ? k_accent : k_bg_hover );
		ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 8.0f );
		const auto text = listening ? std::string( "..." ) : std::format( "[ {} ]", key_name( value ) );

		if ( ImGui::Button( text.c_str( ), { 130.0f, 28.0f } ) && !listening )
		{
			g_listening_key = &value;
			g_listening_start_frame = ImGui::GetFrameCount( );
			g_listening_armed = false;
		}
		pointer_cursor_if_hovered( );
		ImGui::PopStyleVar( );
		ImGui::PopStyleColor( 2 );

		if ( g_listening_key == &value && ImGui::GetFrameCount( ) > g_listening_start_frame )
		{
			if ( !g_listening_armed )
			{

				if ( bind_input_is_down( ) )
				{
					end_row( );
					return;
				}
				g_listening_armed = true;
				end_row( );
				return;
			}

			if ( const int key = pressed_bind_key( ) )
			{
				value = key == VK_ESCAPE ? 0 : key;
				g_listening_key = nullptr;
				g_listening_armed = false;
			}
		}
		end_row( );
	}

	void color_picker_popup( zdraw::rgba& color, ImVec2 item_min, ImVec2 item_max, ImGuiID picker_id )
	{
		auto value = to_imvec( color );
		constexpr auto picker_size = ImVec2{ 312.0f, 314.0f };
		constexpr auto picker_area = 216.0f;
		auto picker_position = ImVec2{ item_max.x - picker_size.x, item_max.y + 8.0f };
		if ( picker_position.y + picker_size.y > settings_bounds_max( ).y - 8.0f ) picker_position.y = item_min.y - picker_size.y - 8.0f;
		picker_position.x = std::clamp( picker_position.x, settings_bounds_min( ).x + 8.0f, settings_bounds_max( ).x - picker_size.x - 8.0f );
		picker_position.y = std::clamp( picker_position.y, settings_bounds_min( ).y + 8.0f, settings_bounds_max( ).y - picker_size.y - 8.0f );
		ImGui::SetNextWindowPos( picker_position, ImGuiCond_Always );
		ImGui::SetNextWindowSize( picker_size, ImGuiCond_Always );
		ImGui::PushStyleColor( ImGuiCol_PopupBg, ImVec4{ 0, 0, 0, 0 } );
		ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, { 0, 0 } );
		ImGui::PushStyleVar( ImGuiStyleVar_PopupRounding, 14.0f );
		ImGui::PushStyleVar( ImGuiStyleVar_PopupBorderSize, 0.0f );
		if ( ImGui::BeginPopup( "##picker", ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground ) )
		{
			const auto popup_min = ImGui::GetWindowPos( );
			const auto popup_max = popup_min + picker_size;
			auto* popup_draw = ImGui::GetWindowDrawList( );

			popup_draw->PushClipRect( settings_bounds_min( ), settings_bounds_max( ), false );
			soft_shadow( popup_draw, popup_min, popup_max, 14.0f, { 0.0f, 6.0f }, 14.0f, { 0, 0, 0, 1 }, 0.55f );
			draw_popup_surface( popup_draw, popup_min, popup_max, 14.0f );
			popup_draw->PopClipRect( );

			float hue{}, saturation{}, brightness{};
			ImGui::ColorConvertRGBtoHSV( value.x, value.y, value.z, hue, saturation, brightness );
			auto& stored_hue = g_color_hues.try_emplace( picker_id, 0.735f ).first->second;
			if ( saturation > 0.001f ) stored_hue = hue;
			else hue = stored_hue;
			const auto sv_min = popup_min + ImVec2{ 16.0f, 16.0f };
			const auto sv_max = sv_min + ImVec2{ picker_area, picker_area };
			float hue_r{}, hue_g{}, hue_b{};
			ImGui::ColorConvertHSVtoRGB( hue, 1.0f, 1.0f, hue_r, hue_g, hue_b );
			const auto hue_color = IM_COL32( static_cast<int>( hue_r * 255.0f ),
				static_cast<int>( hue_g * 255.0f ), static_cast<int>( hue_b * 255.0f ), 255 );
			add_linear_gradient_rounded( popup_draw, sv_min, sv_max, IM_COL32_WHITE,
				hue_color, 8.0f, ImDrawFlags_RoundCornersAll, true );
			add_linear_gradient_rounded( popup_draw, sv_min, sv_max,
				IM_COL32( 0, 0, 0, 0 ), IM_COL32_BLACK, 8.0f,
				ImDrawFlags_RoundCornersAll, false );
			popup_draw->AddRect( sv_min, sv_max, packed( k_border_light ), 8.0f, 0, 1.5f );

			ImGui::SetCursorPos( sv_min - popup_min );
			ImGui::InvisibleButton( "##sv", sv_max - sv_min );
			pointer_cursor_if_hovered( );
			if ( ImGui::IsItemActive( ) )
			{
				const auto mouse = ImGui::GetIO( ).MousePos;
				saturation = std::clamp( ( mouse.x - sv_min.x ) / picker_area, 0.0f, 1.0f );
				brightness = 1.0f - std::clamp( ( mouse.y - sv_min.y ) / picker_area, 0.0f, 1.0f );
				ImGui::ColorConvertHSVtoRGB( hue, saturation, brightness, value.x, value.y, value.z );
				from_imvec( value, color );
			}
			const auto sv_thumb = ImVec2{ sv_min.x + saturation * picker_area, sv_min.y + ( 1.0f - brightness ) * picker_area };
			popup_draw->AddCircle( sv_thumb, 7.0f, IM_COL32( 0, 0, 0, 160 ), 24, 3.0f );
			popup_draw->AddCircle( sv_thumb, 6.0f, IM_COL32_WHITE, 24, 2.0f );

			const auto hue_min = popup_min + ImVec2{ 244.0f, 16.0f };
			const auto hue_max = hue_min + ImVec2{ 16.0f, picker_area };
			for ( int slice = 0; slice < 6; ++slice )
			{
				float r0{}, g0{}, b0{}, r1{}, g1{}, b1{};
				ImGui::ColorConvertHSVtoRGB( static_cast<float>( slice ) / 6.0f, 1, 1, r0, g0, b0 );
				ImGui::ColorConvertHSVtoRGB( static_cast<float>( slice + 1 ) / 6.0f, 1, 1, r1, g1, b1 );
				const auto y0 = hue_min.y + picker_area * slice / 6.0f;
				const auto y1 = hue_min.y + picker_area * ( slice + 1 ) / 6.0f + 0.5f;
				const auto corners = slice == 0 ? ImDrawFlags_RoundCornersTop
					: slice == 5 ? ImDrawFlags_RoundCornersBottom : ImDrawFlags_RoundCornersNone;
				add_linear_gradient_rounded( popup_draw, { hue_min.x, y0 }, { hue_max.x, y1 },
					ImGui::ColorConvertFloat4ToU32( { r0, g0, b0, 1 } ),
					ImGui::ColorConvertFloat4ToU32( { r1, g1, b1, 1 } ),
					7.0f, corners, false );
			}
			popup_draw->AddRect( hue_min, hue_max, packed( k_border_light ), 7.0f );
			ImGui::SetCursorPos( hue_min - popup_min );
			ImGui::InvisibleButton( "##hue", hue_max - hue_min );
			pointer_cursor_if_hovered( );
			if ( ImGui::IsItemActive( ) )
			{
				hue = std::clamp( ( ImGui::GetIO( ).MousePos.y - hue_min.y ) / picker_area, 0.0f, 1.0f );
				stored_hue = hue;
				ImGui::ColorConvertHSVtoRGB( hue, saturation, brightness, value.x, value.y, value.z );
				from_imvec( value, color );
			}
			const auto hue_y = hue_min.y + hue * picker_area;
			popup_draw->AddRectFilled( { hue_min.x - 3.0f, hue_y - 3.0f }, { hue_max.x + 3.0f, hue_y + 3.0f }, IM_COL32_WHITE, 3.0f );
			popup_draw->AddRect( { hue_min.x - 3.0f, hue_y - 3.0f }, { hue_max.x + 3.0f, hue_y + 3.0f }, IM_COL32( 0, 0, 0, 150 ), 3.0f );

			const auto alpha_min = popup_min + ImVec2{ 272.0f, 16.0f };
			const auto alpha_max = alpha_min + ImVec2{ 16.0f, picker_area };
			popup_draw->AddRectFilled( alpha_min, alpha_max, IM_COL32( 112, 112, 120, 255 ), 7.0f );
			for ( int y = 1; y < 13; ++y )
				for ( int x = 0; x < 2; ++x )
					popup_draw->AddRectFilled( alpha_min + ImVec2{ x * 8.0f, y * 16.0f }, alpha_min + ImVec2{ ( x + 1 ) * 8.0f, std::min( ( y + 1 ) * 16.0f, picker_area ) }, ( x + y ) % 2 ? IM_COL32( 100, 100, 106, 255 ) : IM_COL32( 174, 174, 180, 255 ) );
			add_linear_gradient_rounded( popup_draw, alpha_min, alpha_max,
				ImGui::ColorConvertFloat4ToU32( { value.x, value.y, value.z, 1 } ),
				ImGui::ColorConvertFloat4ToU32( { value.x, value.y, value.z, 0 } ),
				7.0f, ImDrawFlags_RoundCornersAll, false );
			popup_draw->AddRect( alpha_min, alpha_max, packed( k_border_light ), 7.0f );
			ImGui::SetCursorPos( alpha_min - popup_min );
			ImGui::InvisibleButton( "##alpha", alpha_max - alpha_min );
			pointer_cursor_if_hovered( );
			if ( ImGui::IsItemActive( ) )
			{
				value.w = 1.0f - std::clamp( ( ImGui::GetIO( ).MousePos.y - alpha_min.y ) / picker_area, 0.0f, 1.0f );
				from_imvec( value, color );
			}
			const auto alpha_y = alpha_min.y + ( 1.0f - value.w ) * picker_area;
			popup_draw->AddRectFilled( { alpha_min.x - 3.0f, alpha_y - 3.0f }, { alpha_max.x + 3.0f, alpha_y + 3.0f }, IM_COL32_WHITE, 3.0f );
			popup_draw->AddRect( { alpha_min.x - 3.0f, alpha_y - 3.0f }, { alpha_max.x + 3.0f, alpha_y + 3.0f }, IM_COL32( 0, 0, 0, 150 ), 3.0f );

			auto& hex = g_color_hex[ picker_id ];
			if ( ImGui::IsWindowAppearing( ) || !ImGui::IsAnyItemActive( ) )
				std::snprintf( hex.data( ), hex.size( ), "#%02X%02X%02X", color.r, color.g, color.b );
			ImGui::SetCursorPos( { 16.0f, 250.0f } );
			ImGui::PushStyleColor( ImGuiCol_FrameBg, ImVec4{ 1, 1, 1, 0.045f } );
			ImGui::PushStyleColor( ImGuiCol_FrameBgHovered, ImVec4{ 1, 1, 1, 0.075f } );
			ImGui::PushStyleColor( ImGuiCol_FrameBgActive, ImVec4{ 1, 1, 1, 0.09f } );
			ImGui::PushStyleColor( ImGuiCol_Border, k_border_light );
			ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 7.0f );
			ImGui::PushStyleVar( ImGuiStyleVar_FrameBorderSize, 1.0f );
			ImGui::SetNextItemWidth( 88.0f );
			if ( ImGui::InputText( "##hex", hex.data( ), hex.size( ), ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue ) )
			{
				unsigned int r{}, g{}, b{};
				const auto* text = hex[ 0 ] == '#' ? hex.data( ) + 1 : hex.data( );
				if ( std::sscanf( text, "%02x%02x%02x", &r, &g, &b ) == 3 )
				{
					color.r = static_cast<std::uint8_t>( r ); color.g = static_cast<std::uint8_t>( g ); color.b = static_cast<std::uint8_t>( b );
				}
			}
			ImGui::SameLine( 0.0f, 8.0f );
			int channels[ 4 ]{ color.r, color.g, color.b, color.a };
			for ( int channel = 0; channel < 4; ++channel )
			{
				ImGui::PushID( channel );
				ImGui::SetNextItemWidth( 42.0f );
				if ( ImGui::InputInt( "##channel", &channels[ channel ], 0, 0 ) )
				{
					channels[ channel ] = std::clamp( channels[ channel ], 0, 255 );
					color.r = static_cast<std::uint8_t>( channels[ 0 ] ); color.g = static_cast<std::uint8_t>( channels[ 1 ] );
					color.b = static_cast<std::uint8_t>( channels[ 2 ] ); color.a = static_cast<std::uint8_t>( channels[ 3 ] );
				}
				ImGui::PopID( );
				if ( channel != 3 ) ImGui::SameLine( 0.0f, 4.0f );
			}
			popup_draw->AddText( popup_min + ImVec2{ 18.0f, 286.0f }, packed( k_text_muted ), "HEX" );
			popup_draw->AddText( popup_min + ImVec2{ 130.0f, 286.0f }, packed( k_text_muted ), "R" );
			popup_draw->AddText( popup_min + ImVec2{ 176.0f, 286.0f }, packed( k_text_muted ), "G" );
			popup_draw->AddText( popup_min + ImVec2{ 222.0f, 286.0f }, packed( k_text_muted ), "B" );
			popup_draw->AddText( popup_min + ImVec2{ 268.0f, 286.0f }, packed( k_text_muted ), "A" );
			ImGui::PopStyleVar( 2 );
			ImGui::PopStyleColor( 4 );
			ImGui::EndPopup( );
		}
		ImGui::PopStyleVar( 3 );
		ImGui::PopStyleColor( );
	}

	void color_row( const char* label, zdraw::rgba& color )
	{
		begin_row( label, 36.0f );
		const auto picker_id = ImGui::GetID( "##picker" );
		ImGui::InvisibleButton( "##preview", { 36.0f, 22.0f } );
		pointer_cursor_if_hovered( );
		if ( ImGui::IsItemClicked( ) ) ImGui::OpenPopup( "##picker" );

		const auto hovered = ImGui::IsItemHovered( );
		const auto item_min = ImGui::GetItemRectMin( );
		const auto item_max = ImGui::GetItemRectMax( );
		const auto expand = hovered ? 1.0f : 0.0f;
		const auto preview_min = ImVec2{ item_min.x - expand, item_min.y - expand * 0.5f };
		const auto preview_max = ImVec2{ item_max.x + expand, item_max.y + expand * 0.5f };
		auto* draw = ImGui::GetWindowDrawList( );
		draw->AddRectFilled( preview_min, preview_max, IM_COL32( 142, 142, 148, 255 ), 8.0f );
		const auto checker_min = preview_min + ImVec2{ 4.0f, 4.0f };
		const auto checker_max = preview_max - ImVec2{ 4.0f, 4.0f };
		for ( int y = 0; y < 3; ++y )
		{
			for ( int x = 0; x < 5; ++x )
			{
				const auto a = ImVec2{ checker_min.x + x * 7.0f, checker_min.y + y * 7.0f };
				const auto b = ImVec2{ std::min( a.x + 7.0f, checker_max.x ), std::min( a.y + 7.0f, checker_max.y ) };
				if ( a.x >= checker_max.x || a.y >= checker_max.y ) continue;
				draw->AddRectFilled( a, b, ( x + y ) % 2 ? IM_COL32( 120, 120, 120, 255 ) : IM_COL32( 185, 185, 185, 255 ) );
			}
		}
		draw->AddRectFilled( preview_min, preview_max, packed( to_imvec( color ) ), 8.0f );
		draw->AddRect( preview_min, preview_max, hovered ? IM_COL32_WHITE : IM_COL32( 255, 255, 255, 51 ), 8.0f );

		color_picker_popup( color, item_min, item_max, picker_id );
		end_row( );
	}

	void humanizer_preview( int amount, int smoothing,
		const config::combat_profile::humanizer_settings& settings )
	{
		const auto origin = ImGui::GetCursorScreenPos( );
		const auto width = ImGui::GetContentRegionAvail( ).x;
		constexpr auto height = 76.0f;
		ImGui::InvisibleButton( "##humanizer_preview", { width, height } );
		auto* draw = ImGui::GetWindowDrawList( );
		const auto left = origin + ImVec2{ 5.0f, height * 0.68f };
		const auto right = origin + ImVec2{ width - 8.0f, height * 0.38f };
		const auto h = std::clamp( amount / 100.0f, 0.0f, 1.0f );
		const auto step_speed = std::clamp( settings.max_step / 15.0f, 0.25f, 4.0f );
		const auto duration = std::clamp(
			( 0.34f + smoothing * 0.032f ) / std::sqrt( step_speed ), 0.28f, 2.5f );
		const auto reaction = ( settings.reaction_min_ms + settings.reaction_max_ms ) * 0.0005f;
		const auto cycle = reaction + duration + 0.45f;
		const auto elapsed = static_cast<float>( ImGui::GetTime( ) );
		const auto local = std::fmod( elapsed, cycle );
		const auto cycle_index = static_cast<int>( elapsed / cycle );
		const auto progress = local <= reaction ? 0.0f
			: std::clamp( ( local - reaction ) / duration, 0.0f, 1.0f );
		const auto gravity = std::clamp( settings.gravity / 9.0f, 0.15f, 2.25f );
		const auto eased = 1.0f - std::pow( 1.0f - progress, gravity );
		const auto bend = ( settings.curve - 0.5f ) * 38.0f * h;
		const auto damping = std::clamp( settings.damping, 0.0f, 1.0f );
		const auto wind = settings.wind * 0.85f * h;
		const auto overshoot_cycle = ( cycle_index * 37 ) % 100
			< static_cast<int>( settings.overshoot_chance );
		const auto overshoot = overshoot_cycle
			? settings.overshoot_amount * 34.0f * h : 0.0f;
		const auto path_point = [ & ]( float t )
		{
			const auto settle = std::sin( t * 3.14159265f );
			const auto overshoot_shape = t < 0.78f
				? std::sin( t / 0.78f * 1.5707963f )
				: 1.0f - ( t - 0.78f ) / 0.22f;
			const auto x = std::lerp( left.x, right.x, t ) + overshoot * overshoot_shape;
			const auto arc = settle * bend;
			const auto oscillation = std::sin( t * 18.0f + 0.7f ) * wind
				* std::pow( 1.0f - t, 0.35f + damping * 2.5f );
			const auto micro = std::sin( t * 71.0f + 1.9f )
				* settings.jitter * 1.4f * h * settle;
			return ImVec2{ x, std::lerp( left.y, right.y, t ) - arc + oscillation + micro };
		};
		ImVec2 previous = path_point( 0.0f );
		for ( int segment = 1; segment <= 48; ++segment )
		{
			const auto t = segment / 48.0f;
			const auto next = path_point( t );
			draw->AddLine( previous, next, IM_COL32( 255, 255, 255, 31 ), 1.0f );
			previous = next;
		}
		const auto deadzone = 2.5f + settings.deadzone * 3.0f;
		draw->AddCircle( right, deadzone, IM_COL32( 255, 255, 255, 48 ), 20, 1.0f );
		draw->AddCircleFilled( right, 1.7f, IM_COL32( 232, 235, 244, 180 ), 16 );
		for ( int tail = 8; tail >= 0; --tail )
		{
			const auto t = std::max( 0.0f, eased - tail * ( 0.012f + damping * 0.01f ) );
			const auto alpha = static_cast<int>( 28 + ( 8 - tail ) * 18 );
			draw->AddCircleFilled( path_point( t ), tail == 0 ? 2.8f : 1.4f,
				IM_COL32( 214, 226, 255, alpha ), 16 );
		}
	}

	template<typename callback_t>
	void settings_popup( ImVec2 anchor_min, ImVec2 anchor_max, int rows, callback_t&& callback )
	{
		auto* parent = ImGui::GetCurrentWindow( );
		const auto parent_min = ImGui::GetWindowPos( );
		const auto parent_size = ImGui::GetWindowSize( );
		const auto parent_max = parent_min + parent_size;
		const auto popup_size = ImVec2{
			std::clamp( parent_size.x - 24.0f, 288.0f, 320.0f ),
			24.0f + rows * k_row_height };
		const auto bounds_min = settings_bounds_min( );
		const auto bounds_max = settings_bounds_max( );
		const auto nested = parent && ( parent->Flags & ImGuiWindowFlags_Popup ) != 0;

		ImVec2 popup_position{};
		if ( nested )
		{

			const auto right = parent_max.x + 8.0f;
			const auto left = parent_min.x - popup_size.x - 8.0f;
			popup_position.x = right + popup_size.x <= bounds_max.x - 8.0f
				? right : left;
			popup_position.y = anchor_min.y - 12.0f;
		}
		else
		{
			popup_position = { anchor_max.x - popup_size.x, anchor_max.y + 8.0f };
			if ( popup_position.y + popup_size.y > bounds_max.y - 8.0f )
				popup_position.y = anchor_min.y - popup_size.y - 8.0f;
		}

		popup_position.x = std::clamp( popup_position.x,
			bounds_min.x + 8.0f, bounds_max.x - popup_size.x - 8.0f );
		popup_position.y = std::clamp( popup_position.y,
			bounds_min.y + 8.0f, bounds_max.y - popup_size.y - 8.0f );
		ImGui::SetNextWindowPos( popup_position, ImGuiCond_Always );
		ImGui::SetNextWindowSize( popup_size, ImGuiCond_Always );
		ImGui::PushStyleColor( ImGuiCol_PopupBg, ImVec4{ 0, 0, 0, 0 } );
		ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, { 16.0f, 0.0f } );
		ImGui::PushStyleVar( ImGuiStyleVar_PopupRounding, 12.0f );
		ImGui::PushStyleVar( ImGuiStyleVar_PopupBorderSize, 0.0f );
		if ( ImGui::BeginPopup( "##settings",
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
			ImGuiWindowFlags_NoBackground ) )
		{
			const auto popup_min = ImGui::GetWindowPos( );
			const auto popup_max = popup_min + popup_size;
			auto* popup_draw = ImGui::GetWindowDrawList( );
			popup_draw->PushClipRect( bounds_min, bounds_max, false );
			soft_shadow( popup_draw, popup_min, popup_max, 12.0f,
				{ 0.0f, 7.0f }, 15.0f, { 0, 0, 0, 1 }, 0.78f );
			draw_popup_surface( popup_draw, popup_min, popup_max, 12.0f );
			popup_draw->PopClipRect( );

			ImGui::SetCursorPos( { 16.0f, 12.0f } );
			callback( );
			ImGui::EndPopup( );
		}
		ImGui::PopStyleVar( 3 );
		ImGui::PopStyleColor( );
	}

	template<typename callback_t>
	void settings_popup_row( const char* label, int rows, callback_t&& callback )
	{
		constexpr auto options_width = 28.0f;
		begin_row( label, options_width );
		ImGui::InvisibleButton( "##settings_options", { options_width, 24.0f } );
		const auto options_min = ImGui::GetItemRectMin( );
		const auto options_max = ImGui::GetItemRectMax( );
		pointer_cursor_if_hovered( );
		if ( ImGui::IsItemClicked( ) ) ImGui::OpenPopup( "##settings" );

		auto* draw = ImGui::GetWindowDrawList( );
		const auto dot_y = ( options_min.y + options_max.y ) * 0.5f;
		for ( int dot = 0; dot < 3; ++dot )
			draw->AddCircleFilled( { options_min.x + 5.0f + dot * 9.0f, dot_y },
				1.08f, IM_COL32_WHITE, 12 );

		settings_popup( options_min, options_max, rows,
			std::forward<callback_t>( callback ) );
		end_row( );
	}

	template<typename callback_t>
	void toggle_popup_row( const char* label, bool& value, int rows, callback_t&& callback )
	{
		constexpr auto options_width = 28.0f;
		constexpr auto spacing = 8.0f;
		constexpr auto switch_width = 46.0f;
		begin_row( label, options_width + spacing + switch_width );

		ImGui::InvisibleButton( "##settings_options", { options_width, 24.0f } );
		const auto options_min = ImGui::GetItemRectMin( );
		const auto options_max = ImGui::GetItemRectMax( );
		pointer_cursor_if_hovered( );
		if ( ImGui::IsItemClicked( ) ) ImGui::OpenPopup( "##settings" );

		auto* draw = ImGui::GetWindowDrawList( );
		const auto dot_y = ( options_min.y + options_max.y ) * 0.5f;
		for ( int dot = 0; dot < 3; ++dot )
		{
			draw->AddCircleFilled( { options_min.x + 5.0f + dot * 9.0f, dot_y }, 1.08f, IM_COL32_WHITE, 12 );
		}

		ImGui::SetCursorScreenPos( { options_max.x + spacing, options_min.y } );
		toggle_control( value );

		settings_popup( options_min, options_max, rows,
			std::forward<callback_t>( callback ) );
		end_row( );
	}

	constexpr int k_bar_settings_rows = 7;
	constexpr int k_weapon_settings_rows = 4;
	constexpr int k_info_flag_settings_rows = 4;

	void dock_player_bar( config::visual_profile::player::layout_element& layout, int dock )
	{
		const auto scale = layout.scale;
		switch ( dock )
		{
		case 0: layout = { -0.07f, 0.50f, scale }; break;
		case 1: layout = { 0.50f, -0.04f, scale }; break;
		case 2: layout = { 0.50f, 1.012f, scale }; break;
		case 3: layout = { 1.04f, 0.50f, scale }; break;
		default: break;
		}
	}

	template<typename bar_t>
	void player_bar_settings_rows( bar_t& bar,
		config::visual_profile::player::layout_element& layout )
	{
		int position = static_cast<int>( bar.position );
		const auto previous_position = position;
		static constexpr const char* positions[]{ "Left", "Top", "Bottom", "Right" };
		select_row( "Position", position, positions );
		bar.position = static_cast<typename bar_t::position_type>( position );
		if ( position != previous_position ) dock_player_bar( layout, position );
		slider_row( "Thickness", bar.thickness, 1.0f, 12.0f, " px", 0.5f );
		toggle_popup_row( "Outline", bar.outline, 2, [ & ]
			{
				slider_row( "Thickness", bar.outline_thickness, 0.5f, 4.0f, " px", 0.5f );
				color_row( "Color", bar.outline_color );
			} );
		toggle_popup_row( "Gradient", bar.gradient, 2, [ & ]
			{
				color_row( "Full Color", bar.full_color );
				color_row( "Low Color", bar.low_color );
			} );
		bool segmented = bar.segments > 1;
		toggle_popup_row( "Segments", segmented, 2, [ & ]
			{
				slider_row( "Count", bar.segments, 2, 10 );
				slider_row( "Gap", bar.segment_gap, 0.0f, 4.0f, " px", 0.5f );
			} );
		if ( segmented && bar.segments < 2 ) bar.segments = 4;
		if ( !segmented ) bar.segments = 1;
		toggle_popup_row( "Show Value", bar.show_value, 1, [ & ]
			{
				color_row( "Text Color", bar.text_color );
			} );
		color_row( "Background Color", bar.background_color );
	}

	void player_weapon_settings_rows( config::visual_profile::player::weapon& weapon )
	{
		int display = static_cast<int>( weapon.display );
		static constexpr const char* displays[]{ "Text", "Icon", "Text + Icon" };
		select_row( "Display", display, displays );
		weapon.display = static_cast<config::visual_profile::player::weapon::display_type>( display );
		color_row( "Text Color", weapon.text_color );
		color_row( "Icon Color", weapon.icon_color );
		toggle_popup_row( "Ammo Indicator", weapon.ammo.enabled, 2, [ & ]
			{
				toggle_row( "Exact Ammo Count", weapon.ammo.show_count );
				color_row( "Empty Color", weapon.ammo.empty_color );
			} );
	}

	config::visual_profile::player::info_flags::style& selected_info_flag_style(
		config::visual_profile::player::info_flags& flags, int selected )
	{
		switch ( std::clamp( selected, 0, 8 ) )
		{
		case 0: return flags.money_style;
		case 1: return flags.armor_style;
		case 2: return flags.kit_style;
		case 3: return flags.scoped_style;
		case 4: return flags.defusing_style;
		case 5: return flags.flashed_style;
		case 6: return flags.ping_style;
		case 7: return flags.distance_style;
		default: return flags.bomb_damage_style;
		}
	}

	void player_info_flag_settings_rows( config::visual_profile::player::info_flags& flags )
	{
		using flag = config::visual_profile::player::info_flags::flag;
		static constexpr std::pair<const char*, int> options[]{
			{ "Money", flag::money }, { "Armor", flag::armor }, { "Defuse Kit", flag::kit },
			{ "Scoped", flag::scoped }, { "Defusing", flag::defusing },
			{ "Flashed", flag::flashed }, { "Ping", flag::ping },
			{ "Distance", flag::distance }, { "Bomb Damage", flag::bomb_damage }
		};
		int mask = flags.flags;
		multiselect_row( "Active Flags", mask, options, ( 1 << 9 ) - 1 );
		flags.flags = static_cast<std::uint16_t>( mask );

		static int selected{};
		static constexpr const char* names[]{
			"Money", "Armor", "Defuse Kit", "Scoped", "Defusing",
			"Flashed", "Ping", "Distance", "Bomb Damage"
		};
		select_row( "Edit Flag", selected, names );
		auto& style = selected_info_flag_style( flags, selected );
		color_row( "Color", style.color );
		slider_row( "Scale", style.scale, 0.55f, 2.0f, "x", 0.05f );
	}

	template<typename callback_t>
	void visual_editor_settings_popup( int rows, callback_t&& callback )
	{
		const auto element_min = ImGui::GetItemRectMin( );
		const auto element_max = ImGui::GetItemRectMax( );
		if ( ImGui::IsItemHovered( ) && ImGui::IsMouseClicked( ImGuiMouseButton_Right ) )
			ImGui::OpenPopup( "##element_settings" );

		const auto popup_size = ImVec2{ 320.0f, 24.0f + rows * k_row_height };
		const auto display = ImGui::GetIO( ).DisplaySize;
		const settings_bounds_scope popup_bounds{ { 0.0f, 0.0f }, display };
		const auto cursor = ImGui::GetMousePos( );
		auto popup_position = ImVec2{ std::max( cursor.x + 10.0f, element_max.x + 10.0f ),
			cursor.y + 8.0f };
		if ( popup_position.x + popup_size.x > display.x - 8.0f )
			popup_position.x = std::min( cursor.x - popup_size.x - 10.0f,
				element_min.x - popup_size.x - 10.0f );
		popup_position.x = std::clamp( popup_position.x, 8.0f, display.x - popup_size.x - 8.0f );
		popup_position.y = std::clamp( popup_position.y, 8.0f, display.y - popup_size.y - 8.0f );
		ImGui::SetNextWindowPos( popup_position, ImGuiCond_Appearing );
		ImGui::SetNextWindowSize( popup_size, ImGuiCond_Always );
		ImGui::PushStyleColor( ImGuiCol_PopupBg, ImVec4{ 0, 0, 0, 0 } );
		ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, { 16.0f, 0.0f } );
		ImGui::PushStyleVar( ImGuiStyleVar_PopupRounding, 12.0f );
		ImGui::PushStyleVar( ImGuiStyleVar_PopupBorderSize, 0.0f );
		if ( ImGui::BeginPopup( "##element_settings",
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
			ImGuiWindowFlags_NoBackground ) )
		{
			const auto popup_min = ImGui::GetWindowPos( );
			const auto popup_max = popup_min + popup_size;
			auto* popup_draw = ImGui::GetWindowDrawList( );
			popup_draw->PushClipRect( settings_bounds_min( ), settings_bounds_max( ), false );
			soft_shadow( popup_draw, popup_min, popup_max, 12.0f, { 0.0f, 7.0f },
				15.0f, { 0, 0, 0, 1 }, 0.78f );
			draw_popup_surface( popup_draw, popup_min, popup_max, 12.0f );
			popup_draw->PopClipRect( );
			ImGui::SetCursorPos( { 16.0f, 12.0f } );
			callback( );
			ImGui::EndPopup( );
		}
		ImGui::PopStyleVar( 3 );
		ImGui::PopStyleColor( );
	}

	constexpr const char* k_chams_materials[]
	{
		"Solid", "Shaded", "Glow", "Glow Outline", "Iridescent", "Water Flow", "Glossy"
	};
	static_assert( std::size( k_chams_materials ) == config::visual_profile::chams::material_type_count );

	[[nodiscard]] int chams_material_row_count( const config::visual_profile::chams::material& m )
	{
		constexpr auto common = 3;
		switch ( m.type )
		{
		case config::visual_profile::chams::shaded:       return common + 2;
		case config::visual_profile::chams::glow:         return common + 1;
		case config::visual_profile::chams::glow_outline: return common + 3;
		case config::visual_profile::chams::iridescent:   return common + 2;
		case config::visual_profile::chams::water_flow:   return common + 1;
		case config::visual_profile::chams::glossy:       return common + 4;
		default:                                 return common;
		}
	}

	void chams_material_rows( config::visual_profile::chams::material& m )
	{
		select_row( "Material", m.type, k_chams_materials );
		color_row( "Color", m.color );
		toggle_row( "Wireframe", m.wireframe );

		switch ( m.type )
		{
		case config::visual_profile::chams::shaded:
			slider_row( "Roughness", m.roughness, 0.0f, 1.0f, "", 0.01f );
			slider_row( "Metalness", m.metalness, 0.0f, 1.0f, "", 0.01f );
			break;

		case config::visual_profile::chams::glow:
			slider_row( "Exponent", m.exponent, 0.1f, 8.0f, "", 0.1f );
			break;

		case config::visual_profile::chams::glow_outline:
			slider_row( "Exponent", m.exponent, 0.1f, 8.0f, "", 0.1f );
			slider_row( "Falloff", m.falloff, 0.05f, 2.0f, "", 0.05f );
			slider_row( "Fresnel Filling", m.fresnel_fill, 0.0f, 1.0f, "", 0.01f );
			break;

		case config::visual_profile::chams::iridescent:
			slider_row( "Strength", m.strength, 0.0f, 1.0f, "", 0.01f );
			slider_row( "Roughness", m.roughness, 0.0f, 1.0f, "", 0.01f );
			break;

		case config::visual_profile::chams::water_flow:
			slider_row( "Speed", m.speed, 0.0f, 4.0f, "", 0.05f );
			break;

		case config::visual_profile::chams::glossy:
			slider_row( "Exponent", m.exponent, 0.1f, 8.0f, "", 0.1f );
			slider_row( "Fresnel Filling", m.fresnel_fill, 0.0f, 1.0f, "", 0.01f );
			slider_row( "Falloff", m.falloff, 0.05f, 2.0f, "", 0.05f );
			color_row( "Tint", m.tint );
			break;

		default:
			break;
		}
	}

	template<typename callback_t>
	void card_in_column( const char* id, const char* title, int rows, int column, callback_t&& callback )
	{
		column = std::clamp( column, 0, 1 );
		const auto card_id = ImGui::GetID( id );
		const auto card_min = ImVec2{ g_cards_origin.x + column * ( g_cards_width + 20.0f ), g_cards_origin.y + g_cards_y[ column ] };
		ImGui::SetCursorScreenPos( card_min );
		const auto target_height = 54.0f + rows * k_row_height;
		auto& card_height = g_card_height_animations[ card_id ];
		if ( card_height <= 0.0f ) card_height = target_height;
		card_height = approach( card_height, target_height, 0.40f );
		const auto card_max = ImVec2{ card_min.x + g_cards_width, card_min.y + card_height };
		ImGui::PushStyleColor( ImGuiCol_ChildBg, k_bg_card );
		ImGui::PushStyleColor( ImGuiCol_Border, k_border );
		ImGui::PushStyleVar( ImGuiStyleVar_ChildRounding, 14.0f );
		ImGui::PushStyleVar( ImGuiStyleVar_ChildBorderSize, 1.0f );
		ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, { 20.0f, 16.0f } );
		ImGui::BeginChild( id, { g_cards_width, card_height }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );

		const auto* title_wrapper = app::context().overlay.fonts( ).menu_semibold_13;
		const auto title_size = title_wrapper && title_wrapper->im_font ? title_wrapper->font_size : ImGui::GetFontSize( );
		const auto title_pos = ImGui::GetCursorScreenPos( );
		draw_section_title( ImGui::GetWindowDrawList( ), title_pos,
			ImGui::GetWindowPos( ).x + ImGui::GetWindowContentRegionMax( ).x, render::localization::tr( title ) );
		ImGui::Dummy( { 0.0f, title_size } );
		ImGui::Dummy( { 0.0f, 3.0f } );
		callback( );

		ImGui::EndChild( );
		ImGui::PopStyleVar( 3 );
		ImGui::PopStyleColor( 2 );
		g_cards_y[ column ] += card_height + 20.0f;
	}

	template<typename callback_t>
	void card( const char* id, const char* title, int rows, callback_t&& callback )
	{
		card_in_column( id, title, rows, g_cards_index++ % 2, std::forward<callback_t>( callback ) );
	}

	bool tab_button( const char* label, bool active )
	{
		const auto* text = render::localization::tr( label );
		const auto text_size = ImGui::CalcTextSize( text );
		const auto size = ImVec2{ text_size.x + 32.0f, text_size.y + 16.0f };
		ImGui::InvisibleButton( label, size );
		const auto clicked = ImGui::IsItemClicked( );
		const auto hovered = ImGui::IsItemHovered( );
		pointer_cursor_if_hovered( );
		const auto id = ImGui::GetItemID( );
		const auto hover_t = animate_state( g_hover_animations, id, hovered );
		const auto active_t = animate_selection( id, active );
		const auto min = ImGui::GetItemRectMin( );
		const auto max = ImGui::GetItemRectMax( );
		auto* draw = ImGui::GetWindowDrawList( );
		if ( active_t > 0.01f )
		{
			soft_shadow( draw, min, max, 20.0f, { 0.0f, 4.0f }, 9.0f, { k_accent.x, k_accent.y, k_accent.z, 1.0f }, active_t * 0.55f );
		}
		const auto idle = mix( k_bg_card, k_bg_hover, hover_t );
		draw->AddRectFilled( min, max, packed( mix( idle, k_accent, active_t ) ), 20.0f );
		if ( active_t < 0.99f && hover_t > 0.01f ) draw->AddRect( min, max, packed( mix( k_border, k_border_light, hover_t ) ), 20.0f );
		draw->AddText( { min.x + 16.0f, min.y + ( size.y - text_size.y ) * 0.5f }, packed( mix( k_text_muted, ImVec4{ 1, 1, 1, 1 }, std::max( hover_t, active_t ) ) ), text );
		return clicked;
	}

	ImVec2 svg_point( ImVec2 center, float x, float y )
	{
		constexpr auto scale = 0.78f;
		return { center.x + ( x - 12.0f ) * scale, center.y + ( y - 12.0f ) * scale };
	}

	void svg_segment( ImDrawList* draw, ImVec2 from, ImVec2 to, ImU32 color, float thickness = 1.65f )
	{
		draw->AddLine( from, to, color, thickness );
		const auto cap_radius = thickness * 0.5f;
		draw->AddCircleFilled( from, cap_radius, color, 12 );
		draw->AddCircleFilled( to, cap_radius, color, 12 );
	}

	void svg_polyline( ImDrawList* draw, std::span<const ImVec2> points, ImU32 color, bool closed = false, float thickness = 1.65f )
	{
		for ( std::size_t i = 1; i < points.size( ); ++i )
		{
			svg_segment( draw, points[ i - 1 ], points[ i ], color, thickness );
		}
		if ( closed && points.size( ) > 2 ) svg_segment( draw, points.back( ), points.front( ), color, thickness );
	}

	void draw_nav_icon( ImDrawList* draw, int icon, ImVec2 center, ImU32 color )
	{
		if ( icon == 0 )
		{
			draw->AddCircle( center, 6.2f, color, 32, 1.65f );
			svg_segment( draw, svg_point( center, 12, 1.5f ), svg_point( center, 12, 4.0f ), color );
			svg_segment( draw, svg_point( center, 12, 20.0f ), svg_point( center, 12, 22.5f ), color );
			svg_segment( draw, svg_point( center, 1.5f, 12 ), svg_point( center, 4.0f, 12 ), color );
			svg_segment( draw, svg_point( center, 20.0f, 12 ), svg_point( center, 22.5f, 12 ), color );
			draw->AddCircleFilled( center, 1.35f, color, 18 );
		}
		else if ( icon == 1 )
		{
			const std::array points{
				svg_point( center, 13, 2 ), svg_point( center, 4, 14 ), svg_point( center, 11, 14 ),
				svg_point( center, 10, 22 ), svg_point( center, 20, 9 ), svg_point( center, 13, 9 ) };
			svg_polyline( draw, points, color, true, 1.75f );
		}
		else if ( icon == 2 )
		{
			draw->PathClear( );
			draw->PathLineTo( svg_point( center, 2, 12 ) );
			draw->PathBezierCubicCurveTo( svg_point( center, 5.0f, 7.0f ), svg_point( center, 8.0f, 5.0f ), svg_point( center, 12, 5.0f ), 12 );
			draw->PathBezierCubicCurveTo( svg_point( center, 16.0f, 5.0f ), svg_point( center, 19.0f, 7.0f ), svg_point( center, 22, 12 ), 12 );
			draw->PathBezierCubicCurveTo( svg_point( center, 19.0f, 17.0f ), svg_point( center, 16.0f, 19.0f ), svg_point( center, 12, 19.0f ), 12 );
			draw->PathBezierCubicCurveTo( svg_point( center, 8.0f, 19.0f ), svg_point( center, 5.0f, 17.0f ), svg_point( center, 2, 12 ), 12 );
			draw->PathStroke( color, ImDrawFlags_Closed, 1.65f );
			draw->AddCircle( center, 3.1f, color, 24, 1.65f );
		}
		else
		{
			constexpr std::array<ImVec2, 4> cells{
				ImVec2{ 4.0f, 4.0f }, ImVec2{ 13.0f, 4.0f },
				ImVec2{ 4.0f, 13.0f }, ImVec2{ 13.0f, 13.0f } };
			for ( const auto& cell : cells )
			{
				draw->AddRect(
					svg_point( center, cell.x, cell.y ),
					svg_point( center, cell.x + 7.0f, cell.y + 7.0f ),
					color, 1.8f, 0, 1.65f );
			}
		}
	}

	bool nav_button( const char* label, bool active, int icon )
	{
		ImGui::InvisibleButton( label, { 200.0f, 48.0f } );
		const auto clicked = ImGui::IsItemClicked( );
		const auto hovered = ImGui::IsItemHovered( );
		pointer_cursor_if_hovered( );
		const auto id = ImGui::GetItemID( );
		const auto hover_t = animate_state( g_hover_animations, id, hovered );
		const auto active_t = animate_selection( id, active );
		const auto min = ImGui::GetItemRectMin( );
		const auto max = ImGui::GetItemRectMax( );
		auto* draw = ImGui::GetWindowDrawList( );
		if ( active_t > 0.01f ) soft_shadow( draw, min, max, 14.0f, { 0.0f, 5.0f }, 10.0f, { k_accent.x, k_accent.y, k_accent.z, 1.0f }, active_t * 0.70f );
		const auto idle = ImVec4{ k_bg_hover.x, k_bg_hover.y, k_bg_hover.z, k_bg_hover.w * hover_t };
		const auto background = mix( idle, k_accent, active_t );
		draw->AddRectFilled( min, max, packed( background ), 14.0f );
		const auto color = packed( mix( k_text_muted, ImVec4{ 1, 1, 1, 1 }, std::max( hover_t, active_t ) ) );
		draw_nav_icon( draw, icon, { min.x + 27.0f, min.y + 24.0f }, color );
		draw->AddText( { min.x + 50.0f, min.y + ( 48.0f - ImGui::GetTextLineHeight( ) ) * 0.5f }, color, render::localization::tr( label ) );
		return clicked;
	}

	void begin_cards( const char* id )
	{
		ImGui::PushID( id );
		g_cards_origin = ImGui::GetCursorScreenPos( );
		g_cards_width = ( ImGui::GetContentRegionAvail( ).x - 20.0f ) * 0.5f;
		g_cards_y[ 0 ] = g_cards_y[ 1 ] = 0.0f;
		g_cards_index = 0;
	}

	void end_cards( )
	{
		const auto content_height = std::max( g_cards_y[ 0 ], g_cards_y[ 1 ] );
		ImGui::SetCursorScreenPos( g_cards_origin );
		ImGui::Dummy( { g_cards_width * 2.0f + 20.0f, std::max( 0.0f, content_height - 20.0f ) } );
		ImGui::PopID( );
	}
}

bool menu_t::initialize( HWND hwnd )
{
	this->m_hwnd = hwnd;
	return true;
}

void menu_t::map_pointer_to_layout(
	float& x, float& y, const float display_width,
	const float display_height ) const noexcept
{
	if ( !this->is_open( ) ) return;
	const auto scale = current_menu_scale( display_width, display_height );
	if ( std::abs( scale - 1.0f ) < 0.0001f ) return;
	const auto origin = menu_transform_origin( display_width, display_height );
	x = origin.x + ( x - origin.x ) / scale;
	y = origin.y + ( y - origin.y ) / scale;
}

void menu_t::map_pointer_to_screen(
	float& x, float& y, const float display_width,
	const float display_height ) const noexcept
{
	if ( !this->is_open( ) ) return;
	const auto scale = current_menu_scale( display_width, display_height );
	if ( std::abs( scale - 1.0f ) < 0.0001f ) return;
	const auto origin = menu_transform_origin( display_width, display_height );
	x = origin.x + ( x - origin.x ) * scale;
	y = origin.y + ( y - origin.y ) * scale;
}

void menu_t::reset_content_animation( )
{
	this->m_content_animation = 0.0f;
}

void menu_t::poll_hotkey( )
{
	const auto menu_key = platform::windows::lifecycle_keys( ).menu;
	const bool hotkey_is_down = ( ::GetAsyncKeyState( menu_key ) & 0x8000 ) != 0;
	if ( hotkey_is_down && !this->m_menu_hotkey_was_down )
	{
		if ( this->m_open.load( std::memory_order_relaxed ) )
		{
			this->m_open.store( false, std::memory_order_release );
			this->m_open_pending = false;
		}
		else this->m_open_pending = !this->m_open_pending;
	}
	this->m_menu_hotkey_was_down = hotkey_is_down;

	if ( !this->m_open_pending || this->m_open.load( std::memory_order_relaxed ) )
		return;

	std::vector<std::uint16_t> movement_keys{ 'W', 'A', 'S', 'D' };
	constexpr std::array movement_actions{
		game::input_action::forward, game::input_action::back,
		game::input_action::left, game::input_action::right,
	};
	for ( const auto action : movement_actions )
	{
		for ( const auto& binding : game::input_bindings( ).candidates( action ) )
		{
			if ( binding.device == game::input_device::keyboard && binding.virtual_key
				&& std::ranges::find( movement_keys, binding.virtual_key )
					== movement_keys.end( ) )
			{
				movement_keys.push_back( binding.virtual_key );
			}
		}
	}
	if ( std::ranges::any_of( movement_keys, [ ]( const std::uint16_t key )
		{
			return app::context( ).input.physical_key_down( key );
		} ) )
	{
		return;
	}

	this->m_open.store( true, std::memory_order_release );
	std::vector<platform::windows::input_gateway::key_transition> releases{};
	releases.reserve( movement_keys.size( ) );
	for ( const auto key : movement_keys ) releases.push_back( { key, false } );
	app::context( ).input.keys( releases );
	app::context( ).input.set_movement_gate( {}, false );
	this->m_open_pending = false;
	g_toggle_animations.clear( );
	this->reset_content_animation( );
}

void menu_t::draw( )
{

	if ( !this->m_open.load( std::memory_order_acquire ) )
	{
		return;
	}
	synchronize_menu_palette( );

	const auto display = ImGui::GetIO( ).DisplaySize;
	const auto menu_scale = current_menu_scale( display.x, display.y );
	const auto transform_origin = menu_transform_origin( display.x, display.y );
	const menu_render_scope render_scope{ display, transform_origin, menu_scale };
	const auto initial_position = initial_menu_layout_position(
		display.x, display.y );
	ImGui::SetNextWindowPos( initial_position, ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( { k_menu_width, k_menu_height }, ImGuiCond_Always );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, { 0.0f, 0.0f } );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 20.0f );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 0.0f );
	ImGui::PushStyleVar( ImGuiStyleVar_ScrollbarSize, 4.0f );
	ImGui::PushStyleVar( ImGuiStyleVar_ScrollbarRounding, 10.0f );
	ImGui::PushStyleColor( ImGuiCol_Text, k_text_main );
	ImGui::PushStyleColor( ImGuiCol_ScrollbarBg, ImVec4{ 0, 0, 0, 0 } );
	ImGui::PushStyleColor( ImGuiCol_ScrollbarGrab, k_border_light );
	ImGui::PushStyleColor( ImGuiCol_ScrollbarGrabHovered, k_text_muted );
	ImGui::Begin( "##vesta_native_menu", nullptr,
		ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings );

	const auto min = ImGui::GetWindowPos( );
	const auto max = ImVec2{ min.x + k_menu_width, min.y + k_menu_height };
	g_menu_min = min;
	g_menu_max = max;
	auto* draw = ImGui::GetWindowDrawList( );
	draw->PushClipRectFullScreen( );
	soft_shadow( draw, min, max, 20.0f, { 0.0f, 14.0f }, 34.0f, { 0, 0, 0, 1 }, 1.0f );
	draw->PopClipRect( );
	draw->AddRectFilled( min, max, packed( k_bg_base ), 20.0f );
	draw->AddRect( min, max, packed( k_border ), 20.0f );
	draw->AddLine( { min.x + 20.0f, min.y + 1.0f }, { max.x - 20.0f, min.y + 1.0f }, IM_COL32( 255, 255, 255, 20 ) );

	push_menu_font( app::context().overlay.fonts( ).menu_regular_12 );
	this->draw_sidebar( );
	this->draw_content( );
	ImGui::PopFont( );

	ImGui::End( );

	push_menu_font( app::context().overlay.fonts( ).menu_regular_12 );
	this->draw_visual_editor( );
	ImGui::PopFont( );
	scale_menu_draw_lists( transform_origin, menu_scale );
	ImGui::PopStyleColor( 4 );
	ImGui::PopStyleVar( 5 );
}

void menu_t::draw_sidebar( )
{
	ImGui::SetCursorPos( { 0.0f, 0.0f } );
	ImGui::BeginChild( "##sidebar", { 240.0f, 650.0f }, false, ImGuiWindowFlags_NoScrollbar );
	const auto min = ImGui::GetWindowPos( );
	const auto max = ImVec2{ min.x + 240.0f, min.y + 650.0f };
	auto* draw = ImGui::GetWindowDrawList( );

	add_vertical_gradient_rounded( draw, min, max, IM_COL32( 0, 0, 0, 26 ), IM_COL32( 0, 0, 0, 76 ), 20.0f, ImDrawFlags_RoundCornersLeft );
	draw->AddLine( { max.x - 1.0f, min.y }, { max.x - 1.0f, max.y }, packed( k_border ) );

	const auto* brand = app::context().overlay.fonts( ).menu_brand_30;
	if ( brand && brand->im_font )
	{
		constexpr auto text = std::string_view{ "VESTA" };
		const auto brand_size = brand->font_size * 1.25f;
		float width{};
		for ( const auto c : text ) width += brand->im_font->CalcTextSizeA( brand_size, FLT_MAX, 0.0f, &c, &c + 1 ).x;
		constexpr auto letter_spacing = 2.5f;
		width += letter_spacing * ( text.size( ) - 1 );
		auto x = min.x + ( 240.0f - width ) * 0.5f;
		for ( const auto c : text )
		{
			draw->AddText( brand->im_font, brand_size, { x, min.y + ( 112.0f - brand_size ) * 0.5f }, IM_COL32_WHITE, &c, &c + 1 );
			x += brand->im_font->CalcTextSizeA( brand_size, FLT_MAX, 0.0f, &c, &c + 1 ).x + letter_spacing;
		}
	}

	static constexpr const char* labels[ ]{ "Aimbot", "Triggerbot", "Visuals", "Misc" };
	ImGui::SetCursorPos( { 20.0f, 112.0f } );
	for ( int i = 0; i < 4; ++i )
	{
		ImGui::SetCursorPosX( 20.0f );
		ImGui::PushID( i );
		const auto active = this->m_page == i;
		if ( nav_button( labels[ i ], active, i ) && !active )
		{
			this->m_page = i;
			this->reset_content_animation( );
		}
		ImGui::PopID( );
		if ( i != 3 ) ImGui::Dummy( { 0.0f, 6.0f } );
	}

	ImGui::EndChild( );
}

void menu_t::draw_content( )
{
	ImGui::SetCursorPos( { 240.0f, 0.0f } );
	ImGui::BeginChild( "##content", { 710.0f, 650.0f }, false, ImGuiWindowFlags_NoScrollbar );
	this->m_content_animation = std::min( 1.0f, this->m_content_animation + ImGui::GetIO( ).DeltaTime / 0.14f );
	const auto t = 1.0f - std::pow( 1.0f - this->m_content_animation, 3.0f );
	ImGui::SetCursorPosY( ( 1.0f - t ) * 4.0f );

	if ( this->m_page == 0 ) this->draw_combat( false );
	else if ( this->m_page == 1 ) this->draw_combat( true );
	else if ( this->m_page == 2 ) this->draw_visuals( );
	else this->draw_misc( );

	ImGui::EndChild( );
}

void menu_t::draw_visual_editor( )
{
	const auto requested = this->m_page == 2 && this->m_visual_group == 0;
	const auto delta_time = std::min( ImGui::GetIO( ).DeltaTime, 1.0f / 30.0f );
	if ( requested )
	{
		this->m_visual_editor_animation = std::min( 1.0f, this->m_visual_editor_animation + delta_time / 0.12f );
	}
	else
	{
		this->m_visual_editor_animation = std::max( 0.0f, this->m_visual_editor_animation - delta_time / 0.07f );
	}
	if ( this->m_visual_editor_animation <= 0.0f ) return;

	const auto progress = std::clamp( this->m_visual_editor_animation, 0.0f, 1.0f );
	const auto reveal = requested ? 1.0f - std::pow( 1.0f - progress, 3.0f ) : progress;
	constexpr auto panel_size = ImVec2{ 320.0f, 650.0f };
	auto display = ImGui::GetIO( ).DisplaySize;
	this->map_pointer_to_layout(
		display.x, display.y, display.x, display.y );
	auto panel_x = g_menu_max.x + 12.0f;
	if ( panel_x + panel_size.x > display.x - 8.0f ) panel_x = std::max( 8.0f, display.x - panel_size.x - 8.0f );
	const auto panel_position = ImVec2{ panel_x + ( 1.0f - reveal ) * 18.0f, g_menu_min.y };

	ImGui::SetNextWindowPos( panel_position, ImGuiCond_Always );
	ImGui::SetNextWindowSize( panel_size, ImGuiCond_Always );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, { 0.0f, 0.0f } );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 20.0f );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 0.0f );
	ImGui::Begin( "##esp_visual_editor", nullptr,
		ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar );

	const auto panel_min = ImGui::GetWindowPos( );
	const auto panel_max = panel_min + panel_size;
	auto* draw = ImGui::GetWindowDrawList( );
	draw->PushClipRectFullScreen( );
	soft_shadow( draw, panel_min, panel_max, 20.0f, { 0.0f, 14.0f }, 30.0f, { 0, 0, 0, 1 }, reveal * 0.85f );
	draw->PopClipRect( );
	draw->AddRectFilled( panel_min, panel_max, packed( k_bg_base ), 20.0f );
	draw->AddRect( panel_min, panel_max, packed( k_border ), 20.0f );
	draw->AddLine( { panel_min.x + 20.0f, panel_min.y + 1.0f }, { panel_max.x - 20.0f, panel_min.y + 1.0f }, IM_COL32( 255, 255, 255, 20 ) );
	draw_section_title( draw, panel_min + ImVec2{ 20.0f, 20.0f }, panel_max.x - 20.0f, render::localization::tr( "ESP EDITOR" ) );

	const auto stage_min = panel_min + ImVec2{ 16.0f, 54.0f };
	const auto stage_max = panel_min + ImVec2{ 304.0f, 634.0f };

	const auto image_size = ImVec2{ 280.0f, 420.0f };

	constexpr auto composition_top = -16.0f;
	constexpr auto composition_bottom = 380.0f;
	const auto composition_height = composition_bottom - composition_top;
	const auto image_min = ImVec2{
		stage_min.x + ( stage_max.x - stage_min.x - image_size.x ) * 0.5f,
		stage_min.y + ( stage_max.y - stage_min.y - composition_height ) * 0.5f - composition_top };
	const auto image_max = image_min + image_size;

	const auto viewport_width = static_cast<std::uint32_t>( std::max( 16.0f, image_size.x ) );
	const auto viewport_height = static_cast<std::uint32_t>( std::max( 16.0f, image_size.y ) );

	auto& chams_cfg = config::visual_settings.m_chams;

	const config::visual_profile::chams::material* chams_material{};
	if ( chams_cfg.enabled )
	{
		if ( chams_cfg.visible.enabled ) chams_material = &chams_cfg.visible;
		else if ( chams_cfg.invisible.enabled ) chams_material = &chams_cfg.invisible;
	}

	const auto& seen_model = chams::g_renderer.diag( ).last_model_path;
	const auto model_path = seen_model.empty( )
		? std::string{ "agents/models/ctm_sas/ctm_sas.vmdl_c" }
		: seen_model;

	ID3D11ShaderResourceView* viewport_srv{};
	static_cast<void>( chams::g_renderer.ensure_vpk( ) );
	if ( chams::g_renderer.vpk_ready( ) )
	{
		viewport_srv = chams::g_preview.render( chams::g_renderer.vpk( ),
			model_path, viewport_width, viewport_height, chams_material );
	}

	const auto texture = viewport_srv ? viewport_srv : app::context().overlay.ct_preview_texture( );
	draw->PushClipRect( stage_min + ImVec2{ 1.0f, 1.0f }, stage_max - ImVec2{ 1.0f, 1.0f }, true );
	if ( texture )
	{
		const auto texture_id = static_cast<ImTextureID>( reinterpret_cast<std::uintptr_t>( texture ) );
		draw->AddImage( ImTextureRef{ texture_id }, image_min, image_max, { 0, 0 }, { 1, 1 }, IM_COL32_WHITE );
	}
	else
	{
		draw->AddText( image_min + ImVec2{ 72.0f, 190.0f }, packed( k_text_muted ), render::localization::tr( "PREVIEW UNAVAILABLE" ) );
	}

	auto& player = config::visual_settings.m_player;
	const auto point = [ & ]( float x, float y )
		{
			return image_min + ImVec2{ image_size.x * x, image_size.y * y };
		};
	const auto unit_min = point( 0.185f, 0.035f );
	const auto unit_max = point( 0.800f, 0.828f );
	const auto unit_size = unit_max - unit_min;

	if ( player.m_box.enabled )
	{
		zdraw::draw_list box_draw{ draw };
		const auto x = unit_min.x;
		const auto y = unit_min.y;
		const auto width = unit_size.x;
		const auto height = unit_size.y;
		if ( player.m_box.fill )
		{
			box_draw.add_rect_filled( x + 1.0f, y + 1.0f, width - 2.0f, height - 2.0f, { 60, 200, 100, 80 } );
		}
		if ( player.m_box.style == config::visual_profile::player::box::style_type::cornered )
		{
			const auto corner = std::min( player.m_box.corner_length, std::min( width, height ) * 0.4f );
			if ( player.m_box.outline )
			{
				box_draw.add_rect_cornered( x - 1.0f, y - 1.0f, width + 2.0f, height + 2.0f, { 0, 0, 0, 180 }, corner + 1.0f, 1.0f );
				box_draw.add_rect_cornered( x, y, width, height, { 0, 0, 0, 200 }, corner, 2.0f );
			}
			box_draw.add_rect_cornered( x, y, width, height, player.m_box.visible_color, corner, 1.0f );
		}
		else
		{
			if ( player.m_box.outline )
			{
				box_draw.add_rect( x - 1.0f, y - 1.0f, width + 2.0f, height + 2.0f, { 0, 0, 0, 180 }, 1.0f );
				box_draw.add_rect( x, y, width, height, { 0, 0, 0, 200 }, 2.0f );
			}
			box_draw.add_rect( x, y, width, height, player.m_box.visible_color, 1.0f );
		}
	}

	std::array<ImVec2, 24> skeleton_points{};
	for ( const auto bone : k_preview_skeleton_bones )
	{
		bool projected{};
		if ( viewport_srv )
		{
			foundation::vec3 world{};
			float x{}, y{};
			if ( chams::g_preview.bone_position( static_cast<std::uint32_t>( bone ), world )
				&& chams::g_preview.project( world, x, y ) )
			{
				skeleton_points[ bone ] = image_min + ImVec2{ x, y };
				projected = true;
			}
		}

		if ( !projected )
		{
			const auto normalized = k_preview_bones[ bone ];
			skeleton_points[ bone ] = point( normalized.x, normalized.y );
		}
	}

	const auto live_hitboxes = game::hitbox_data().snapshot( );

	const auto project_radius = [ & ]( const foundation::vec3& center, const foundation::vec3& axis,
		float radius, float cx, float cy ) -> float
		{
			const auto view_dir = ( center - chams::g_preview.eye( ) ).normalized( );
			auto perp = axis.cross( view_dir );
			if ( perp.length( ) < 0.001f )
			{
				perp = foundation::vec3{ 0.0f, 0.0f, 1.0f }.cross( view_dir );
				if ( perp.length( ) < 0.001f ) perp = foundation::vec3{ 1.0f, 0.0f, 0.0f };
			}
			perp.normalize( );

			float ex{}, ey{};
			if ( !chams::g_preview.project( center + perp * radius, ex, ey ) ) return 0.0f;
			return std::sqrt( ( ex - cx ) * ( ex - cx ) + ( ey - cy ) * ( ey - cy ) );
		};

	if ( player.m_threat_module.enabled )
	{
		const auto& threat = player.m_threat_module;
		const auto alpha_color = [ & ]( zdraw::rgba color, float alpha )
			{
				color.a = static_cast<std::uint8_t>( std::clamp( alpha, 0.0f, 255.0f ) );
				return packed( to_imvec( color ) );
			};
		const auto draw_capsule = [ & ]( ImVec2 from, ImVec2 to, float radius, const zdraw::rgba& color )
			{
				const auto outline_radius = radius + std::max( 1.0f, threat.outline_thickness );
				const auto draw_shape = [ & ]( float shape_radius, ImU32 shape_color )
				{
					const auto delta = to - from;
					const auto axis_length = std::sqrt( delta.x * delta.x + delta.y * delta.y );
					if ( axis_length < 1.0f )
					{
						draw->AddCircleFilled( from, shape_radius, shape_color, 24 );
						return;
					}

					constexpr auto segments = 12;
					constexpr auto pi = std::numbers::pi_v<float>;
					const auto phi = std::atan2( delta.y, delta.x );
					std::array<ImVec2, ( segments + 1 ) * 2> points{};
					int count{};
					for ( int segment = 0; segment <= segments; ++segment )
					{
						const auto angle = phi + pi * 0.5f + pi * static_cast<float>( segment ) / segments;
						points[ count++ ] = from + ImVec2{ std::cos( angle ), std::sin( angle ) } * shape_radius;
					}
					for ( int segment = 0; segment <= segments; ++segment )
					{
						const auto angle = phi - pi * 0.5f + pi * static_cast<float>( segment ) / segments;
						points[ count++ ] = to + ImVec2{ std::cos( angle ), std::sin( angle ) } * shape_radius;
					}
					draw->AddConvexPolyFilled( points.data( ), count, shape_color );
				};

				if ( threat.outline_alpha > 0.0f )
					draw_shape( outline_radius, alpha_color( color, threat.outline_alpha ) );
				if ( threat.fill_alpha > 0.0f )
					draw_shape( radius, alpha_color( color, threat.fill_alpha ) );
			};

		const auto hitbox_axis = [ & ]( int bone ) -> std::tuple<ImVec2, ImVec2, float>
			{
				if ( viewport_srv && live_hitboxes.count > 0 )
				{
					for ( const auto& hitbox : live_hitboxes )
					{
						if ( hitbox.bone != bone ) continue;

						foundation::vec3 from_world{}, to_world{};
						if ( !chams::g_preview.bone_transform( static_cast<std::uint32_t>( bone ), hitbox.mins, from_world )
							|| !chams::g_preview.bone_transform( static_cast<std::uint32_t>( bone ), hitbox.maxs, to_world ) )
						{
							break;
						}

						float ax{}, ay{}, bx{}, by{}, cx{}, cy{};
						const auto center_world = ( from_world + to_world ) * 0.5f;
						if ( !chams::g_preview.project( from_world, ax, ay )
							|| !chams::g_preview.project( to_world, bx, by )
							|| !chams::g_preview.project( center_world, cx, cy ) )
						{
							break;
						}

						const auto radius_px = project_radius( center_world, to_world - from_world,
							hitbox.radius, cx, cy );

						return { image_min + ImVec2{ ax, ay }, image_min + ImVec2{ bx, by }, radius_px };
					}
				}

				if ( viewport_srv && live_hitboxes.count > 0 ) return { {}, {}, 0.0f };

				const auto it = std::find_if( k_preview_hitboxes.begin( ), k_preview_hitboxes.end( ),
					[ & ]( const preview_hitbox_geometry& hitbox ) { return hitbox.bone == bone; } );
				if ( it == k_preview_hitboxes.end( ) ) return { {}, {}, 0.0f };
				return { point( it->from.x, it->from.y ), point( it->to.x, it->to.y ), it->radius };
			};
		const auto draw_hitbox = [ & ]( int bone, const zdraw::rgba& color )
			{
				const auto [ from, to, radius ] = hitbox_axis( bone );
				if ( radius > 0.0f ) draw_capsule( from, to, radius, color );
			};
		const auto draw_group = [ & ]( std::span<const int> bones, bool enabled, const zdraw::rgba& color )
			{
				if ( !enabled ) return;
				for ( const auto bone : bones ) draw_hitbox( bone, color );
			};

		draw_group( features::visuals::player_t::threat_head_bones, threat.head_hitbox, threat.head_color );
		draw_group( features::visuals::player_t::threat_body_bones, threat.body_hitbox, threat.body_color );
		draw_group( features::visuals::player_t::threat_limb_bones, threat.limb_hitbox, threat.limb_color );
	}
	if ( player.m_skeleton.enabled )
	{
		const auto skeleton_color = packed( to_imvec( player.m_skeleton.visible_color ) );
		for ( const auto& [ from, to ] : features::visuals::player_t::skeleton_connections )
		{
			draw->AddLine( skeleton_points[ from ], skeleton_points[ to ], skeleton_color, std::max( 1.0f, player.m_skeleton.thickness ) );
		}
	}
	if ( player.m_head_circle.enabled )
	{

		auto circle_center = skeleton_points[ 7 ];
		auto circle_radius = 19.5f;

		if ( viewport_srv )
		{
			for ( const auto& hitbox : live_hitboxes )
			{
				if ( game::hitbox_data().hitgroup_from_hitbox( hitbox.index ) != 1 ) continue;

				foundation::vec3 center_world{};
				const auto center_local = ( hitbox.mins + hitbox.maxs ) * 0.5f;
				float cx{}, cy{};
				if ( !chams::g_preview.bone_transform( static_cast<std::uint32_t>( hitbox.bone ), center_local, center_world )
							|| !chams::g_preview.project( center_world, cx, cy ) )
				{
					break;
				}

				circle_center = image_min + ImVec2{ cx, cy };
				circle_radius = project_radius( center_world, foundation::vec3{ 0.0f, 0.0f, 0.0f },
							hitbox.radius, cx, cy );
				break;
			}
		}

		if ( circle_radius > 0.0f )
		{
			draw->AddCircle( circle_center, circle_radius, packed( to_imvec( player.m_head_circle.color ) ), 40,
				std::max( 1.0f, player.m_head_circle.thickness ) );
		}
	}
	if ( player.m_view_line.enabled )
	{

		auto view_line_end = skeleton_points[ 7 ] + ImVec2{ -32.0f, -7.0f };
		if ( viewport_srv )
		{
			foundation::vec3 head{};
			float x{}, y{};

			foundation::vec3 forward{};
			if ( chams::g_preview.bone_position( 7, head )
				&& chams::g_preview.bone_direction( 7, foundation::vec3{ 1.0f, 0.0f, 0.0f }, forward )
				&& chams::g_preview.project( head + forward * std::max( 1.0f, player.m_view_line.length ), x, y ) )
			{
				view_line_end = image_min + ImVec2{ x, y };
			}
		}

		draw->AddLine( skeleton_points[ 7 ], view_line_end,
			packed( to_imvec( player.m_view_line.color ) ), std::max( 1.0f, player.m_view_line.thickness ) );
	}

	const auto anchor = [ & ]( const config::visual_profile::player::layout_element& element )
		{
			return ImVec2{
				config::visual_profile::player::resolve_layout_axis( unit_min.x, unit_max.x, element.x,
					config::visual_profile::player::layout_reference_width ),
				config::visual_profile::player::resolve_layout_axis( unit_min.y, unit_max.y, element.y,
					config::visual_profile::player::layout_reference_height ) };
		};
	const auto draw_preview_text = [ & ]( std::string_view text, ImFont* font, float size, ImVec2 center, ImU32 color )
		{
			const auto measured = font->CalcTextSizeA( size, FLT_MAX, 0.0f, text.data( ), text.data( ) + text.size( ) );
			const auto position = center - measured * 0.5f;
			const auto outline = IM_COL32( 0, 0, 0, 225 );
			draw->AddText( font, size, position + ImVec2{ -1, 0 }, outline, text.data( ), text.data( ) + text.size( ) );
			draw->AddText( font, size, position + ImVec2{ 1, 0 }, outline, text.data( ), text.data( ) + text.size( ) );
			draw->AddText( font, size, position + ImVec2{ 0, -1 }, outline, text.data( ), text.data( ) + text.size( ) );
			draw->AddText( font, size, position + ImVec2{ 0, 1 }, outline, text.data( ), text.data( ) + text.size( ) );
			draw->AddText( font, size, position, color, text.data( ), text.data( ) + text.size( ) );
			return std::pair{ position, position + measured };
		};
	const std::array dock_points{
		ImVec2{ unit_min.x - 12.0f, ( unit_min.y + unit_max.y ) * 0.5f },
		ImVec2{ ( unit_min.x + unit_max.x ) * 0.5f, unit_min.y - 12.0f },
		ImVec2{ ( unit_min.x + unit_max.x ) * 0.5f, unit_max.y + 12.0f },
		ImVec2{ unit_max.x + 12.0f, ( unit_min.y + unit_max.y ) * 0.5f }
	};
	const auto interact = [ & ]( int id, config::visual_profile::player::layout_element& element,
		ImVec2 item_min, ImVec2 item_max, bool allow_docking,
		int settings_rows, auto&& settings )
		{
			int dock_target = -1;
			const auto constrain = [ & ]( ImVec2 min, ImVec2 max )
				{
					ImVec2 correction{};
					if ( min.x < stage_min.x ) correction.x = stage_min.x - min.x;
					else if ( max.x > stage_max.x ) correction.x = stage_max.x - max.x;
					if ( min.y < stage_min.y ) correction.y = stage_min.y - min.y;
					else if ( max.y > stage_max.y ) correction.y = stage_max.y - max.y;
					element.x += correction.x / unit_size.x;
					element.y += correction.y / unit_size.y;
				};
			constrain( item_min, item_max );
			constexpr auto hit_padding = 5.0f;
			const auto hit_min = item_min - ImVec2{ hit_padding, hit_padding };
			const auto hit_max = item_max + ImVec2{ hit_padding, hit_padding };
			ImGui::PushID( id );
			ImGui::SetCursorScreenPos( hit_min );
			ImGui::InvisibleButton( "##preview_element", hit_max - hit_min );
			const auto hovered = ImGui::IsItemHovered( );
			if ( hovered ) ImGui::SetMouseCursor( ImGuiMouseCursor_ResizeAll );
			if ( ImGui::IsItemActive( ) && ImGui::IsMouseDragging( ImGuiMouseButton_Left ) )
			{
				auto delta = ImGui::GetIO( ).MouseDelta;
				delta.x = std::clamp( delta.x, stage_min.x - item_min.x, stage_max.x - item_max.x );
				delta.y = std::clamp( delta.y, stage_min.y - item_min.y, stage_max.y - item_max.y );
				element.x += delta.x / unit_size.x;
				element.y += delta.y / unit_size.y;
			}
			const auto dragging = ImGui::IsItemActive( )
				&& ImGui::IsMouseDragging( ImGuiMouseButton_Left );
			if ( allow_docking && ( dragging || ImGui::IsItemDeactivated( ) ) )
			{
				if ( dragging )
				{
					for ( const auto& point : dock_points )
					{
						draw->AddCircleFilled( point, 4.5f, IM_COL32( 26, 25, 34, 235 ), 18 );
						draw->AddCircle( point, 5.0f, IM_COL32( 255, 255, 255, 92 ), 18, 1.2f );
					}
				}
				const auto mouse = ImGui::GetMousePos( );
				auto nearest_distance = 28.0f;
				for ( int point = 0; point < static_cast<int>( dock_points.size( ) ); ++point )
				{
					const auto delta = mouse - dock_points[ point ];
					const auto distance = std::sqrt( delta.x * delta.x + delta.y * delta.y );
					if ( distance < nearest_distance )
					{
						nearest_distance = distance;
						dock_target = point;
					}
				}
				if ( dragging && dock_target >= 0 )
				{
					draw->AddCircleFilled( dock_points[ dock_target ], 8.0f,
						packed( ImVec4{ k_accent.x, k_accent.y, k_accent.z, 0.32f } ), 24 );
					draw->AddCircle( dock_points[ dock_target ], 7.0f,
						packed( k_accent ), 24, 2.0f );
				}
				if ( !ImGui::IsItemDeactivated( ) ) dock_target = -1;
			}
			if ( hovered && ImGui::GetIO( ).MouseWheel != 0.0f )
			{
				element.scale = std::clamp( element.scale + ImGui::GetIO( ).MouseWheel * 0.08f, 0.55f, 2.0f );
			}
			if ( hovered || ImGui::IsItemActive( ) )
			{
				draw->AddRect( hit_min, hit_max, IM_COL32( 255, 255, 255, 150 ), 5.0f, 0, 1.0f );
			}
			visual_editor_settings_popup( settings_rows, std::forward<decltype( settings )>( settings ) );
			ImGui::PopID( );
			return dock_target;
		};

	const auto* regular_wrapper = app::context().overlay.fonts( ).menu_regular_12;
	const auto* semibold_wrapper = app::context().overlay.fonts( ).menu_semibold_13;
	const auto* weapon_wrapper = app::context().overlay.fonts( ).weapons_15;
	auto* regular_font = regular_wrapper && regular_wrapper->im_font ? regular_wrapper->im_font : ImGui::GetFont( );
	auto* semibold_font = semibold_wrapper && semibold_wrapper->im_font ? semibold_wrapper->im_font : ImGui::GetFont( );
	auto* weapon_font = weapon_wrapper && weapon_wrapper->im_font ? weapon_wrapper->im_font : ImGui::GetFont( );
	const auto regular_size = regular_wrapper ? regular_wrapper->font_size : ImGui::GetFontSize( );
	const auto semibold_size = semibold_wrapper ? semibold_wrapper->font_size : ImGui::GetFontSize( );
	const auto weapon_size = weapon_wrapper ? weapon_wrapper->font_size : ImGui::GetFontSize( );

	if ( player.m_name.enabled )
	{
		const auto bounds = draw_preview_text( "PLAYER", semibold_font, semibold_size * player.m_layout.name.scale,
			anchor( player.m_layout.name ), packed( to_imvec( player.m_name.color ) ) );
		interact( 0, player.m_layout.name, bounds.first, bounds.second, false, 1, [ & ]
			{
				color_row( "Color", player.m_name.color );
			} );
	}
	if ( player.m_weapon.enabled )
	{
		const auto show_icon = player.m_weapon.display == config::visual_profile::player::weapon::display_type::icon ||
			player.m_weapon.display == config::visual_profile::player::weapon::display_type::text_and_icon;
		const auto show_text = player.m_weapon.display == config::visual_profile::player::weapon::display_type::text ||
			player.m_weapon.display == config::visual_profile::player::weapon::display_type::text_and_icon;
		const auto icon = features::visuals::player_t::weapon_glyph( "m4a1_silencer" );
		const auto center = anchor( player.m_layout.weapon );
		const auto icon_size = weapon_size * player.m_layout.weapon.scale;
		const auto text_size = regular_size * player.m_layout.weapon.scale;
		const auto icon_extent = show_icon ? weapon_font->CalcTextSizeA( icon_size, FLT_MAX, 0.0f, icon.data( ), icon.data( ) + icon.size( ) ) : ImVec2{};
		constexpr std::string_view weapon_name{ "M4A1-S" };
		const auto text_extent = show_text ? regular_font->CalcTextSizeA( text_size, FLT_MAX, 0.0f,
			weapon_name.data( ), weapon_name.data( ) + weapon_name.size( ) ) : ImVec2{};
		const auto gap = show_icon && show_text ? 2.0f * player.m_layout.weapon.scale : 0.0f;
		const auto total_height = icon_extent.y + text_extent.y + gap;
		auto current_y = center.y - total_height * 0.5f;
		auto item_min = ImVec2{ FLT_MAX, FLT_MAX };
		auto item_max = ImVec2{ -FLT_MAX, -FLT_MAX };
		if ( show_icon )
		{
			const auto icon_center = ImVec2{ center.x, current_y + icon_extent.y * 0.5f };
			auto bounds = draw_preview_text( icon, weapon_font, icon_size, icon_center,
				packed( to_imvec( player.m_weapon.ammo.enabled
					? player.m_weapon.ammo.empty_color : player.m_weapon.icon_color ) ) );
			if ( player.m_weapon.ammo.enabled )
			{
				constexpr auto preview_ammo_fraction = 20.0f / 30.0f;
				const auto split_x = bounds.first.x + ( bounds.second.x - bounds.first.x )
					* ( 1.0f - preview_ammo_fraction );
				draw->PushClipRect( { split_x, bounds.first.y - 2.0f },
					bounds.second + ImVec2{ 2.0f, 2.0f }, true );
				draw_preview_text( icon, weapon_font, icon_size, icon_center,
					packed( to_imvec( player.m_weapon.icon_color ) ) );
				draw->PopClipRect( );
			}
			item_min = { std::min( item_min.x, bounds.first.x ), std::min( item_min.y, bounds.first.y ) };
			item_max = { std::max( item_max.x, bounds.second.x ), std::max( item_max.y, bounds.second.y ) };
			current_y += icon_extent.y + gap;
		}
		if ( show_text )
		{
			const auto bounds = draw_preview_text( weapon_name, regular_font, text_size,
				{ center.x, current_y + text_extent.y * 0.5f }, packed( to_imvec( player.m_weapon.text_color ) ) );
			item_min = { std::min( item_min.x, bounds.first.x ), std::min( item_min.y, bounds.first.y ) };
			item_max = { std::max( item_max.x, bounds.second.x ), std::max( item_max.y, bounds.second.y ) };
		}
		if ( show_icon || show_text )
			interact( 1, player.m_layout.weapon, item_min, item_max, false,
				k_weapon_settings_rows, [ & ] { player_weapon_settings_rows( player.m_weapon ); } );
	}
	const auto draw_preview_bar = [ & ]( const auto& bar,
		const config::visual_profile::player::layout_element& layout, float fraction )
	{
		const auto center = anchor( layout );
		const auto scale = layout.scale;
		const auto position = static_cast<int>( bar.position );
		const auto vertical = position == 0 || position == 3;
		const auto thickness = std::clamp( bar.thickness, 1.0f, 12.0f ) * scale;
		const auto size = vertical ? ImVec2{ thickness, unit_size.y * scale }
			: ImVec2{ unit_size.x * scale, thickness };
		const auto bar_min = center - size * 0.5f;
		const auto bar_max = center + size * 0.5f;
		const auto outline = std::clamp( bar.outline_thickness, 0.5f, 4.0f ) * scale;
		if ( bar.outline )
			draw->AddRectFilled( bar_min - ImVec2{ outline, outline },
				bar_max + ImVec2{ outline, outline }, packed( to_imvec( bar.outline_color ) ) );
		draw->AddRectFilled( bar_min, bar_max, packed( to_imvec( bar.background_color ) ) );
		const auto clamped = std::clamp( fraction, 0.0f, 1.0f );
		const auto fill_min = vertical
			? ImVec2{ bar_min.x, bar_max.y - size.y * clamped }
			: bar_min;
		const auto fill_max = vertical
			? bar_max
			: ImVec2{ bar_min.x + size.x * clamped, bar_max.y };
		if ( bar.gradient )
		{
			if ( vertical )
				draw->AddRectFilledMultiColor( fill_min, fill_max,
					packed( to_imvec( bar.full_color ) ), packed( to_imvec( bar.full_color ) ),
					packed( to_imvec( bar.low_color ) ), packed( to_imvec( bar.low_color ) ) );
			else
				draw->AddRectFilledMultiColor( fill_min, fill_max,
					packed( to_imvec( bar.low_color ) ), packed( to_imvec( bar.full_color ) ),
					packed( to_imvec( bar.full_color ) ), packed( to_imvec( bar.low_color ) ) );
		}
		else draw->AddRectFilled( fill_min, fill_max, packed( to_imvec( bar.full_color ) ) );

		const auto segments = std::clamp( bar.segments, 1, 10 );
		const auto gap = std::clamp( bar.segment_gap, 0.0f, 4.0f ) * scale;
		const auto separator = packed( to_imvec( bar.outline ? bar.outline_color : bar.background_color ) );
		for ( int segment = 1; segment < segments && gap > 0.0f; ++segment )
		{
			const auto ratio = static_cast<float>( segment ) / segments;
			if ( vertical )
			{
				const auto y = bar_min.y + size.y * ratio;
				draw->AddRectFilled( { bar_min.x, y - gap * 0.5f }, { bar_max.x, y + gap * 0.5f }, separator );
			}
			else
			{
				const auto x = bar_min.x + size.x * ratio;
				draw->AddRectFilled( { x - gap * 0.5f, bar_min.y }, { x + gap * 0.5f, bar_max.y }, separator );
			}
		}
		return std::pair{ bar_min - ImVec2{ outline, outline },
			bar_max + ImVec2{ outline, outline } };
	};
	if ( player.m_health_bar.enabled )
	{
		const auto bounds = draw_preview_bar( player.m_health_bar, player.m_layout.health, 0.76f );
		const auto dock = interact( 2, player.m_layout.health, bounds.first, bounds.second, true,
			k_bar_settings_rows, [ & ] { player_bar_settings_rows( player.m_health_bar, player.m_layout.health ); } );
		if ( dock >= 0 )
		{
			dock_player_bar( player.m_layout.health, dock );
			player.m_health_bar.position =
				static_cast<config::visual_profile::player::health_bar::position_type>( dock );
		}
	}
	if ( player.m_armor_bar.enabled )
	{
		const auto bounds = draw_preview_bar( player.m_armor_bar, player.m_layout.armor, 0.84f );
		const auto dock = interact( 3, player.m_layout.armor, bounds.first, bounds.second, true,
			k_bar_settings_rows, [ & ] { player_bar_settings_rows( player.m_armor_bar, player.m_layout.armor ); } );
		if ( dock >= 0 )
		{
			dock_player_bar( player.m_layout.armor, dock );
			player.m_armor_bar.position =
				static_cast<config::visual_profile::player::armor_bar::position_type>( dock );
		}
	}
	{

		const auto& info = player.m_info_flags;
		using flag = config::visual_profile::player::info_flags::flag;

		struct preview_flag
		{
			std::string_view text;
			const config::visual_profile::player::info_flags::style* style{};
			flag kind{};
		};
		std::array<preview_flag, 9> flags{};
		std::size_t flag_count{};
		const auto add_flag = [ & ]( bool shown, std::string_view text,
			const config::visual_profile::player::info_flags::style& style, flag kind )
		{
			if ( shown && flag_count < flags.size( ) )
			{
				flags[ flag_count++ ] = { text, &style, kind };
			}
		};

		add_flag( info.enabled && info.has( flag::money ), "$4200", info.money_style, flag::money );
		add_flag( info.enabled && info.has( flag::armor ), "100 HK", info.armor_style, flag::armor );
		add_flag( info.enabled && info.has( flag::scoped ), "ZOOM", info.scoped_style, flag::scoped );
		add_flag( info.enabled && info.has( flag::defusing ), "DEFUSING", info.defusing_style, flag::defusing );
		add_flag( info.enabled && info.has( flag::ping ), "42MS", info.ping_style, flag::ping );
		add_flag( info.enabled && info.has( flag::distance ), "18M", info.distance_style, flag::distance );
		add_flag( info.enabled && info.has( flag::bomb_damage ), "-48 HP", info.bomb_damage_style, flag::bomb_damage );
		add_flag( info.enabled && info.has( flag::kit ), "KIT", info.kit_style, flag::kit );
		add_flag( info.enabled && info.has( flag::flashed ), "FLASHED", info.flashed_style, flag::flashed );

		if ( flag_count > 0 )
		{
			const auto scale = player.m_layout.flags.scale;
			const auto start = anchor( player.m_layout.flags );
			auto current_y = start.y;
			auto max_width = 0.0f;
			for ( std::size_t i = 0; i < flag_count; ++i )
			{
				const auto& [ text, style, kind ] = flags[ i ];
				const auto size = regular_size * 0.72f * scale * style->scale;
				const auto measured = regular_font->CalcTextSizeA( size, FLT_MAX, 0.0f, text.data( ), text.data( ) + text.size( ) );
				if ( kind == flag::kit )
				{
					const auto glyph_size = weapon_size * scale * style->scale;
					constexpr std::string_view glyph{ "r" };
					const auto extent = weapon_font->CalcTextSizeA( glyph_size, FLT_MAX, 0.0f,
						glyph.data( ), glyph.data( ) + glyph.size( ) );
					draw->AddText( weapon_font, glyph_size, { start.x, current_y },
						packed( to_imvec( style->color ) ), glyph.data( ), glyph.data( ) + glyph.size( ) );
					max_width = std::max( max_width, extent.x );
					current_y += extent.y;
				}
				else if ( kind == flag::flashed )
				{
					const auto glyph_size = weapon_size * scale * style->scale;
					constexpr std::string_view glyph{ "i" };
					const auto extent = weapon_font->CalcTextSizeA( glyph_size, FLT_MAX, 0.0f,
						glyph.data( ), glyph.data( ) + glyph.size( ) );
					draw->AddText( weapon_font, glyph_size, { start.x, current_y },
						packed( to_imvec( style->color ) ), glyph.data( ), glyph.data( ) + glyph.size( ) );
					max_width = std::max( max_width, extent.x );
					current_y += extent.y;
				}
				else
				{
					draw->AddText( regular_font, size, { start.x, current_y },
						packed( to_imvec( style->color ) ), text.data( ), text.data( ) + text.size( ) );
					max_width = std::max( max_width, measured.x );
					current_y += measured.y;
				}
			}
			interact( 5, player.m_layout.flags, start, { start.x + max_width, current_y }, false,
				k_info_flag_settings_rows, [ & ] { player_info_flag_settings_rows( player.m_info_flags ); } );
		}
	}

	draw->PopClipRect( );

	if ( viewport_srv )
	{
		const auto& io = ImGui::GetIO( );
		const auto inside = io.MousePos.x >= image_min.x && io.MousePos.x <= image_max.x
			&& io.MousePos.y >= image_min.y && io.MousePos.y <= image_max.y;

		if ( !ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
		{
			g_preview_orbiting = false;
		}
		else if ( !g_preview_orbiting && ImGui::IsMouseClicked( ImGuiMouseButton_Left )
			&& inside && !ImGui::IsAnyItemActive( ) )
		{
			g_preview_orbiting = true;
		}

		if ( g_preview_orbiting )
		{
			chams::g_preview.orbit( io.MouseDelta.x );
			ImGui::SetMouseCursor( ImGuiMouseCursor_ResizeEW );
		}

	}

	ImGui::End( );
	ImGui::PopStyleVar( 3 );
}

void menu_t::draw_combat( bool triggerbot )
{
	static constexpr const char* groups[ ]{ "Global", "Pistol", "SMG", "Rifle", "Shotgun", "Sniper", "Heavy" };
	ImGui::SetCursorPos( { 24.0f, 24.0f } );
	for ( int i = 0; i < 7; ++i )
	{
		if ( i ) ImGui::SameLine( 0.0f, 10.0f );
		if ( tab_button( groups[ i ], this->m_weapon_group == i - 1 ) && this->m_weapon_group != i - 1 )
		{
			this->m_weapon_group = i - 1;
			this->reset_content_animation( );
		}
	}

	ImGui::SetCursorPos( { 14.0f, 68.0f } );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, { 0.0f, 0.0f } );
	ImGui::BeginChild( triggerbot ? "##trigger_cards" : "##aim_cards", { 682.0f, 568.0f }, false );
	begin_cards( triggerbot ? "##trigger_grid" : "##aim_grid" );

	auto& global = config::combat_settings.global;
	static constexpr const char* activation_modes[ ]{ "Hold", "Always On", "Toggle" };
	static constexpr const char* fov_modes[ ]{ "Fixed", "Distance", "Target" };
	const auto checks_row = [ & ]( config::combat_profile::legit_checks& checks )
	{
		int mask = ( checks.airborne ? 1 : 0 ) | ( checks.smoke ? 2 : 0 )
			| ( checks.flashed ? 4 : 0 );
		static constexpr std::pair<const char*, int> options[]{
			{ "Jump", 1 }, { "Smoke", 2 }, { "Flash", 4 } };
		multiselect_row( "Checks", mask, options, 7 );
		checks.airborne = ( mask & 1 ) != 0;
		checks.smoke = ( mask & 2 ) != 0;
		checks.flashed = ( mask & 4 ) != 0;
	};
	const auto visibility_row = [ & ]( config::combat_profile::legit_checks& checks )
	{
		auto only_visible = checks.walls == config::combat_profile::wall_policy::block;
		toggle_row( "Only Visible", only_visible );
		checks.walls = only_visible ? config::combat_profile::wall_policy::block
			: config::combat_profile::wall_policy::penetration;
	};
	const auto trigger_timing_row = [ & ]( int& delay, int& randomize,
		float& outlier_chance, int& outlier_delay, int& delay_after )
	{
		settings_popup_row( "Shot Timing", 5, [ & ]
			{
				slider_row( "Delay Before", delay, 0, 500, " ms" );
				slider_row( "Randomize", randomize, 0, 100, " ms" );
				slider_row( "Outlier Chance", outlier_chance, 0.0f, 25.0f, "%", 0.5f );
				slider_row( "Outlier Delay", outlier_delay, 0, 500, " ms" );
				slider_row( "Delay After", delay_after, 0, 1000, " ms" );
			} );
	};
	const auto prediction_row = [ & ]( config::combat_profile::prediction_settings& prediction )
	{
		toggle_popup_row( "Prediction", prediction.enabled, 2, [ & ]
			{
				slider_row( "Max Horizon", prediction.max_horizon_ms,
					0.0f, 120.0f, " ms", 1.0f );
				toggle_row( "Acceleration", prediction.acceleration );
			} );
	};
	const auto master_row = [ & ]( bool& enabled, int& mode )
	{
		toggle_popup_row( "Master Switch", enabled, 1, [ & ]
			{
				select_row( "Mode", mode, activation_modes );
			} );
	};
	const auto damage_override_row = [ & ]( bool& enabled, float& value,
		int& mode, int& key )
	{
		const auto rows = mode == config::combat_profile::activation::always ? 2 : 3;
		toggle_popup_row( "Damage Override", enabled, rows, [ & ]
			{
				select_row( "Mode", mode, activation_modes );
				if ( mode != config::combat_profile::activation::always )
					keybind_row( "Key", key );
				slider_row( "Override Damage", value, 1.0f, 100.0f, "", 1.0f );
			} );
	};
	const auto fov_row = [ & ]( int& fixed_fov,
		config::combat_profile::fov_settings& fov, bool& draw_area,
		zdraw::rgba& color )
	{
		const auto dynamic = fov.selection != config::combat_profile::fov_settings::fixed;
		settings_popup_row( "FOV", dynamic ? 4 : 3, [ & ]
			{
				select_row( "Mode", fov.selection, fov_modes );
				if ( !dynamic )
				{
					slider_row( "Radius", fixed_fov, 1, 360, "°" );
				}
				else
				{
					slider_row( "Max Radius", fov.near_fov, 2.0f, 45.0f, "°", 0.5f );
					settings_popup_row( "Advanced", 5, [ & ]
						{
							slider_row( "Full Size At", fov.near_distance_m, 0.5f, 10.0f, " m", 0.5f );
							slider_row( "Max Radius", fov.near_fov, 2.0f, 45.0f, "°", 0.5f );
							slider_row( "Min Size At", fov.far_distance_m, 10.0f, 100.0f, " m", 0.5f );
							slider_row( "Min Radius", fov.far_fov, 0.25f, 15.0f, "°", 0.25f );
							slider_row( "Falloff", fov.distance_curve, 0.25f, 4.0f, "", 0.05f );
							} );
				}
				toggle_color_row( "Visualization", draw_area, color );
			} );
	};
	const auto humanizer_row = [ & ]( int& amount, int& smoothing,
		config::combat_profile::humanizer_settings& humanizer )
	{
		settings_popup_row( "Humanizer Profile", 6, [ & ]
			{
				humanizer_preview( amount, smoothing, humanizer );
				slider_row( "Amount", amount, 0, 100, "%" );
				slider_row( "Smoothing", smoothing, 0, 50 );
				settings_popup_row( "Motion", 4, [ & ]
					{
						slider_row( "Gravity", humanizer.gravity, 0.0f, 20.0f, "", 0.05f );
						slider_row( "Wind", humanizer.wind, 0.0f, 20.0f, "", 0.05f );
						slider_row( "Max Step", humanizer.max_step, 1.0f, 90.0f, "°", 0.5f );
						slider_row( "Damping", humanizer.damping, 0.0f, 1.0f, "", 0.01f );
					} );
				settings_popup_row( "Behavior", 7, [ & ]
					{
						slider_row( "Reaction Min", humanizer.reaction_min_ms, 0, 500, " ms" );
						slider_row( "Reaction Max", humanizer.reaction_max_ms, 0, 750, " ms" );
						slider_row( "Curve", humanizer.curve, 0.0f, 1.0f, "", 0.01f );
						slider_row( "Overshoot Chance", humanizer.overshoot_chance, 0.0f, 100.0f, "%", 1.0f );
						slider_row( "Overshoot Amount", humanizer.overshoot_amount, 0.0f, 1.0f, "", 0.01f );
						slider_row( "Jitter", humanizer.jitter, 0.0f, 3.0f, "", 0.05f );
						slider_row( "Deadzone", humanizer.deadzone, 0.0f, 2.0f, "°", 0.05f );
					} );
			} );
	};
	const auto multipoint_row = [ & ]( bool& enabled,
		config::combat_profile::multipoint_settings& settings )
	{
		toggle_popup_row( "Multi-Point", enabled, 5, [ & ]
			{
				toggle_row( "Cap Points", settings.caps );
				toggle_row( "Side Points", settings.sides );
				slider_row( "Head Scale", settings.head_scale, 0.05f, 0.95f, "", 0.05f );
				slider_row( "Body Scale", settings.body_scale, 0.05f, 0.95f, "", 0.05f );
				slider_row( "Limbs Scale", settings.limb_scale, 0.05f, 0.95f, "", 0.05f );
			} );
	};
	const auto rcs_row = [ & ]( config::combat_profile::rcs_settings& rcs )
	{
		toggle_popup_row( "RCS", rcs.enabled, 6, [ & ]
			{
				slider_row( "Start Bullet", rcs.start_bullet, 1, 10 );
				slider_row( "Pitch Strength", rcs.pitch, 0.0f, 200.0f, "%", 1.0f );
				slider_row( "Yaw Strength", rcs.yaw, 0.0f, 200.0f, "%", 1.0f );
				slider_row( "Correction Time", rcs.response_ms, 1.0f, 150.0f, " ms", 1.0f );
				slider_row( "Strength Variation", rcs.randomness, 0.0f, 30.0f, "%", 1.0f );
				slider_row( "Path Drift", rcs.drift, 0.0f, 30.0f, "%", 1.0f );
			} );
	};
	if ( this->m_weapon_group < 0 )
	{
		if ( !triggerbot )
		{
			const auto aim_uses_key = global.aimbot_activation_mode
				!= config::combat_profile::activation::always;
			const auto targeting_rows = global.aimbot_enabled
				? ( aim_uses_key ? 9 : 8 ) : 1;
			card_in_column( "targeting", "TARGETING", targeting_rows, 0, [ & ]
				{
					master_row( global.aimbot_enabled, global.aimbot_activation_mode );
					if ( global.aimbot_enabled )
					{
						if ( aim_uses_key ) keybind_row( "Key", global.aimbot_key );
						checks_row( global.aimbot_checks );
						aim_parts_row( global.aimbot_hitbox_parts );
						fov_row( global.aimbot_fov, global.aimbot_fov_config,
							global.aimbot_draw_fov, global.aimbot_fov_color );
						humanizer_row( global.aimbot_humanize,
							global.aimbot_smoothing, global.aimbot_humanizer );
						multipoint_row( global.aimbot_multipoint,
							global.aimbot_multipoint_config );
						prediction_row( global.aimbot_prediction );
						toggle_row( "Lethal Only", global.aimbot_lethal_only );
					}
				} );
			card_in_column( "recoil", "RECOIL CONTROL", 1, 1, [ & ]
				{ rcs_row( global.aimbot_rcs ); } );
			if ( global.aimbot_enabled )
				card_in_column( "penetration", "PENETRATION", 3, 1, [ & ]
					{
						visibility_row( global.aimbot_checks );
						slider_row( "Min Damage", global.aimbot_min_damage, 1.0f, 100.0f, "", 1.0f );
						damage_override_row( global.aimbot_min_damage_override_enabled,
							global.aimbot_min_damage_override,
							global.aimbot_min_damage_override_mode,
							global.aimbot_min_damage_override_key );
					} );
		}
		else
		{
			static constexpr const char* seed_types[ ]{ "None", "Restricted", "Unrestricted" };
			const auto global_seeded = global.triggerbot_seed_type != config::combat_profile::seed_mode::none;
			const auto trigger_uses_key = global.triggerbot_activation_mode
				!= config::combat_profile::activation::always;
			const auto trigger_rows = global.triggerbot_enabled
				? ( trigger_uses_key ? 1 : 0 ) + ( global_seeded ? 7 : 8 ) : 1;
			card_in_column( "trigger_core", "TRIGGERBOT CORE", trigger_rows, 0, [ & ]
				{
					master_row( global.triggerbot_enabled, global.triggerbot_activation_mode );
					if ( global.triggerbot_enabled )
					{
						if ( trigger_uses_key ) keybind_row( "Key", global.triggerbot_key );
						checks_row( global.triggerbot_checks );
						select_row( "Seed Type", global.triggerbot_seed_type, seed_types );
						aim_parts_row( global.triggerbot_hitbox_parts );
						if ( global_seeded )
							slider_row( "Reaction Time (ms)", global.triggerbot_reaction_time, 0, 400 );
						else
						{
							slider_row( "Hitchance", global.triggerbot_hitchance, 0.0f, 100.0f, "%", 1.0f );
							trigger_timing_row( global.triggerbot_delay,
								global.triggerbot_randomize_ms, global.triggerbot_outlier_chance,
								global.triggerbot_outlier_delay_ms, global.triggerbot_delay_after_ms );
						}
						toggle_row( "Predictive", global.triggerbot_predictive );
						toggle_row( "Lethal Only", global.triggerbot_lethal_only );
					}
				} );
			if ( global.triggerbot_enabled )
				card_in_column( "trigger_penetration", "PENETRATION", 3, 1, [ & ]
					{
						visibility_row( global.triggerbot_checks );
						slider_row( "Min Damage", global.triggerbot_min_damage, 1.0f, 100.0f, "", 1.0f );
						damage_override_row( global.triggerbot_min_damage_override_enabled,
							global.triggerbot_min_damage_override,
							global.triggerbot_min_damage_override_mode,
							global.triggerbot_min_damage_override_key );
					} );
		}
	}
	else
	{
		auto& group = config::combat_settings.overrides[ this->m_weapon_group ];
		card( "override", "OVERRIDE SETTINGS", 1, [ & ] { toggle_row( "Inherit Global Settings", group.use_global ); } );
		if ( !group.use_global && !triggerbot && global.aimbot_enabled )
		{
			card_in_column( "targeting", "TARGETING", 7, 0, [ & ]
				{
					checks_row( group.aimbot_checks );
					aim_parts_row( group.aimbot_hitbox_parts );
					fov_row( group.aimbot_fov, group.aimbot_fov_config,
						global.aimbot_draw_fov, global.aimbot_fov_color );
					humanizer_row( group.aimbot_humanize,
						group.aimbot_smoothing, group.aimbot_humanizer );
					multipoint_row( group.aimbot_multipoint,
						group.aimbot_multipoint_config );
					prediction_row( group.aimbot_prediction );
					toggle_row( "Lethal Only", group.aimbot_lethal_only );
				} );
			card_in_column( "recoil", "RECOIL CONTROL", 1, 1, [ & ]
				{ rcs_row( group.aimbot_rcs ); } );
			card( "penetration", "PENETRATION", 2, [ & ]
				{
					visibility_row( group.aimbot_checks );
					slider_row( "Min Damage", group.aimbot_min_damage, 1.0f, 100.0f, "", 1.0f );
				} );
		}
		else if ( !group.use_global && triggerbot && global.triggerbot_enabled )
		{
			static constexpr const char* seed_types[ ]{ "None", "Restricted", "Unrestricted" };
			const auto group_seeded = group.triggerbot_seed_type != config::combat_profile::seed_mode::none;
			card_in_column( "trigger_core", "TRIGGERBOT CORE", group_seeded ? 6 : 7, 0, [ & ]
				{
					checks_row( group.triggerbot_checks );
					select_row( "Seed Type", group.triggerbot_seed_type, seed_types );
					aim_parts_row( group.triggerbot_hitbox_parts );
					if ( group_seeded )
					{
						slider_row( "Reaction Time (ms)", group.triggerbot_reaction_time, 0, 400 );
					}
					else
					{
						slider_row( "Hitchance", group.triggerbot_hitchance, 0.0f, 100.0f, "%", 1.0f );
						trigger_timing_row( group.triggerbot_delay,
							group.triggerbot_randomize_ms, group.triggerbot_outlier_chance,
							group.triggerbot_outlier_delay_ms, group.triggerbot_delay_after_ms );
					}
					toggle_row( "Predictive", group.triggerbot_predictive );
					toggle_row( "Lethal Only", group.triggerbot_lethal_only );
				} );
			card_in_column( "trigger_penetration", "PENETRATION", 2, 1, [ & ]
				{
					visibility_row( group.triggerbot_checks );
					slider_row( "Min Damage", group.triggerbot_min_damage, 1.0f, 100.0f, "", 1.0f );
				} );
		}
	}

	end_cards( );
	ImGui::EndChild( );
	ImGui::PopStyleVar( );
}

void menu_t::draw_visuals( )
{
	static constexpr const char* tabs[ ]{ "Players", "Items", "Projectiles", "Bomb", "Radar", "Effects", "Crosshair" };
	ImGui::SetCursorPos( { 24.0f, 24.0f } );
	for ( int i = 0; i < 7; ++i )
	{
		if ( i ) ImGui::SameLine( 0.0f, 10.0f );
		if ( tab_button( tabs[ i ], this->m_visual_group == i ) && this->m_visual_group != i )
		{
			this->m_visual_group = i;
			this->reset_content_animation( );
		}
	}

	ImGui::SetCursorPos( { 14.0f, 68.0f } );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, { 0.0f, 0.0f } );
	ImGui::BeginChild( "##visual_cards", { 682.0f, 568.0f }, false );
	begin_cards( "##visual_grid" );

	if ( this->m_visual_group == 0 )
	{
		auto& p = config::visual_settings.m_player;
		card( "master", "MASTER", 2, [ & ]
			{
				const auto activation_rows = p.activation_mode
					== config::visual_profile::player::always_on ? 2 : 3;
				toggle_popup_row( "Enable Player ESP", p.enabled, activation_rows, [ & ]
					{
						static constexpr const char* modes[]{ "Always On", "Hold", "Toggle" };
						select_row( "Mode", p.activation_mode, modes );
						if ( p.activation_mode != config::visual_profile::player::always_on )
							keybind_row( "Key", p.activation_key );
						toggle_row( "Spectator Sync", p.spectator_sync );
					} );
				toggle_popup_row( "Legit Sync", p.m_legit_sync.enabled, 6, [ & ]
					{
						toggle_row( "Direct Visibility", p.m_legit_sync.direct_visible );
						toggle_row( "Radar Spotted", p.m_legit_sync.radar );
						toggle_row( "Audible Sounds", p.m_legit_sync.sound );
						slider_row( "Radar Hold", p.m_legit_sync.radar_hold, 0.1f, 10.0f, "s", 0.1f );
						slider_row( "Sound Hold", p.m_legit_sync.sound_hold, 0.1f, 10.0f, "s", 0.1f );
						slider_row( "Hearing Distance", p.m_legit_sync.sound_distance, 100.0f, 2500.0f, "u", 10.0f );
					} );
			} );

		card( "box", "BOUNDING BOX", p.m_box.style == config::visual_profile::player::box::style_type::cornered ? 4 : 3, [ & ]
			{
				toggle_row( "Show Box", p.m_box.enabled );
				int style = static_cast<int>( p.m_box.style );
				static constexpr const char* styles[ ]{ "Full", "Cornered" };
				select_row( "Style", style, styles );
				p.m_box.style = static_cast<config::visual_profile::player::box::style_type>( style );
				if ( style == 1 ) slider_row( "Corner Length", p.m_box.corner_length, 4.0f, 30.0f, "", 0.5f );
				toggle_popup_row( "Background Fill", p.m_box.fill, 4, [ & ]
					{
						color_row( "Box Visible Color", p.m_box.visible_color );
						color_row( "Box Occluded Color", p.m_box.occluded_color );
						color_row( "Fill Visible Color", p.m_box.fill_visible_color );
						color_row( "Fill Occluded Color", p.m_box.fill_occluded_color );
					} );
			} );
		card( "details", "DETAILS", 8, [ & ]
			{
				toggle_color_row( "Show Name", p.m_name.enabled, p.m_name.color );
				toggle_popup_row( "Show Weapon", p.m_weapon.enabled, k_weapon_settings_rows, [ & ]
					{
						player_weapon_settings_rows( p.m_weapon );
					} );
				toggle_popup_row( "Health Bar", p.m_health_bar.enabled, k_bar_settings_rows, [ & ]
					{
						player_bar_settings_rows( p.m_health_bar, p.m_layout.health );
					} );
				toggle_popup_row( "Armor Bar", p.m_armor_bar.enabled, k_bar_settings_rows, [ & ]
					{
						player_bar_settings_rows( p.m_armor_bar, p.m_layout.armor );
					} );
				toggle_popup_row( "Info Flags", p.m_info_flags.enabled, k_info_flag_settings_rows, [ & ]
					{
						player_info_flag_settings_rows( p.m_info_flags );
					} );
				auto& chams_cfg = config::visual_settings.m_chams;

				toggle_popup_row( "Chams", chams_cfg.enabled, 5, [ & ]
					{
						toggle_row( "Antialiasing", chams_cfg.antialiasing );
						toggle_popup_row( "Model Glow", chams_cfg.glow_effect.enabled, 3, [ & ]
							{
								color_row( "Glow Color", chams_cfg.glow_effect.color );
								slider_row( "Glow Radius", chams_cfg.glow_effect.radius, 0.5f, 16.0f, "u", 0.5f );
								slider_row( "Glow Strength", chams_cfg.glow_effect.strength, 0.0f, 1.0f, "", 0.01f );
							} );
						toggle_popup_row( "Death Shatter", chams_cfg.kill_effect.enabled, 4, [ & ]
							{
								color_row( "Particle Color", chams_cfg.kill_effect.color );
								slider_row( "Particle Duration", chams_cfg.kill_effect.duration, 0.2f, 3.0f, "s", 0.05f );
								slider_row( "Particle Size", chams_cfg.kill_effect.size, 1.0f, 8.0f, "px", 0.5f );
								slider_row( "Particle Count", chams_cfg.kill_effect.count, 4, 32 );
							} );
						toggle_popup_row( "On Hit Chams", chams_cfg.on_shot.enabled, 2, [ & ]
							{
								slider_row( "Ghost Duration", chams_cfg.on_shot.duration, 0.1f, 3.0f, "s", 0.05f );
								settings_popup_row( "Ghost Appearance",
									chams_material_row_count( chams_cfg.on_shot.appearance ), [ & ]
									{
										chams_material_rows( chams_cfg.on_shot.appearance );
									} );
							} );
						settings_popup_row( "Occlusion", 2, [ & ]
							{
								toggle_row( "Dynamic Doors", chams_cfg.occlude_dynamic_doors );
								toggle_row( "Smoke Occlusion", chams_cfg.occlude_smoke );
							} );
					} );
				toggle_popup_row( "Chams Visible", chams_cfg.visible.enabled,
					chams_material_row_count( chams_cfg.visible ), [ & ]
					{
						chams_material_rows( chams_cfg.visible );
					} );
				toggle_popup_row( "Chams Occluded", chams_cfg.invisible.enabled,
					chams_material_row_count( chams_cfg.invisible ), [ & ]
					{
						chams_material_rows( chams_cfg.invisible );
					} );
			} );
		card( "skeleton", "SKELETON & HITBOXES", 6, [ & ]
			{
				toggle_color_row( "Show Skeleton", p.m_skeleton.enabled, p.m_skeleton.visible_color );
				toggle_color_row( "Show Head Circle", p.m_head_circle.enabled, p.m_head_circle.color );
				toggle_color_row( "Show View Line", p.m_view_line.enabled, p.m_view_line.color );
				toggle_popup_row( "Offscreen Arrows", p.m_offscreen_arrows.enabled, 4, [ & ]
					{
						color_row( "Arrow Color", p.m_offscreen_arrows.color );
						slider_row( "Arrow Size", p.m_offscreen_arrows.size, 6.0f, 40.0f, "", 0.5f );
						slider_row( "Arrow Radius", p.m_offscreen_arrows.radius, 40.0f, 500.0f, "", 1.0f );
						toggle_popup_row( "Bloom", p.m_offscreen_arrows.bloom, 5, [ & ]
							{
								color_row( "Bloom Color", p.m_offscreen_arrows.bloom_color );
								slider_row( "Bloom Radius", p.m_offscreen_arrows.bloom_radius,
									1.0f, 16.0f, "px", 0.5f );
								slider_row( "Bloom Speed", p.m_offscreen_arrows.bloom_speed,
									0.05f, 2.0f, "Hz", 0.05f );
								slider_row( "Minimum Glow", p.m_offscreen_arrows.bloom_min_alpha,
									0.0f, 1.0f, "", 0.01f );
								slider_row( "Maximum Glow", p.m_offscreen_arrows.bloom_max_alpha,
									0.0f, 1.0f, "", 0.01f );
							} );
					} );
				toggle_popup_row( "Threat Hitboxes", p.m_threat_module.enabled, 3, [ & ]
					{
						color_row( "Head Hitbox Color", p.m_threat_module.head_color );
						color_row( "Body Hitbox Color", p.m_threat_module.body_color );
						slider_row( "Fill Alpha", p.m_threat_module.fill_alpha, 0.0f, 255.0f, "", 1.0f );
					} );

				auto& s = config::visual_settings.m_sound;
				toggle_popup_row( "Sound ESP", s.enabled, 4, [ & ]
					{
						color_row( "Ring Color", s.color );
						toggle_row( "Local Sync", s.local_sync );
						slider_row( "Duration", s.duration, 0.5f, 4.0f, " s", 0.05f );
						slider_row( "Ring Radius", s.radius, 10.0f, 80.0f, "", 0.5f );
					} );
			} );
	}
	else if ( this->m_visual_group == 1 )
	{
		auto& p = config::visual_settings.m_item;
		card( "item_settings", "SETTINGS", 4, [ & ]
			{
				toggle_row( "Enable Item ESP", p.enabled );
				slider_row( "Max Distance", p.max_distance, 5.0f, 150.0f, "", 1.0f );
				toggle_color_row( "Show Icon", p.m_icon.enabled, p.m_icon.color );

				toggle_row( "Show Item Name", p.m_name.enabled );
			} );
		card( "item_filters", "FILTERS", 6, [ & ]
			{
				toggle_row( "Rifles", p.m_filters.rifles );
				toggle_row( "SMGs", p.m_filters.smgs );
				toggle_row( "Snipers", p.m_filters.snipers );
				toggle_row( "Pistols", p.m_filters.pistols );
				toggle_row( "Grenades", p.m_filters.grenades );
				toggle_row( "Utility", p.m_filters.utility );
			} );
	}
	else if ( this->m_visual_group == 2 )
	{
		auto& p = config::visual_settings.m_projectile;
		card( "projectile_elements", "VISUAL ELEMENTS", 4, [ & ]
			{
				toggle_row( "Enable Projectiles", p.enabled );
				toggle_row( "Show Icon", p.show_icon );
				toggle_row( "Effect Timer Ring", p.show_timer_ring );
				toggle_row( "Inferno Bounds", p.show_inferno_bounds );
			} );
		card( "projectile_colors", "COLORS", 5, [ & ]
			{
				color_row( "HE Grenade", p.color_he );
				color_row( "Flashbang", p.color_flash );
				color_row( "Smoke", p.color_smoke );
				color_row( "Molotov", p.color_molotov );
				color_row( "Decoy", p.color_decoy );
			} );
		card( "projectile_indicator", "INDICATOR", 3, [ & ]
			{
				color_row( "Timer Full", p.timer_high_color );
				color_row( "Timer Low", p.timer_low_color );
				color_row( "Indicator Background", p.indicator_background );
			} );
		card( "projectile_inferno", "INFERNO GRADIENT", 2, [ & ]
			{
				slider_row( "Gradient Width", p.inferno_gradient_width, 8.0f, 80.0f, " px", 1.0f );
				slider_row( "Gradient Opacity", p.inferno_gradient_opacity, 0.0f, 100.0f, "%", 1.0f );
			} );
	}
	else if ( this->m_visual_group == 3 )
	{
		auto& p = config::visual_settings.m_bomb;
		card( "bomb_states", "STATES", 5, [ & ]
			{
				toggle_row( "Enable Bomb ESP", p.enabled );
				toggle_color_row( "Show Carrier", p.show_active_bomb, p.active_bomb_color );
				toggle_popup_row( "Show Planted", p.show_planted_bomb, 2, [ & ]
					{
						color_row( "Planted Color", p.bomb_color_t );
						color_row( "Defusing Color", p.bomb_color_ct );
					} );
				toggle_row( "World Timer Ring", p.show_timer );
				toggle_popup_row( "Bomb Info Panel", p.show_info_panel, 2, [ & ]
					{
						color_row( "Timer Color", p.timer_text_color );
						color_row( "Panel Background", p.panel_background );
					} );
			} );
		card( "bomb_safe_zone", "SAFE ZONE (BAKED)", 1, [ & ]
			{
				toggle_popup_row( "Safe Zone Contour", p.show_safe_zone, 4, [ & ]
					{
						color_row( "Zone Color", p.safe_zone_color );
						slider_row( "Gradient Bands", p.safe_zone_bands, 1, 8 );
						slider_row( "Band Step", p.safe_zone_band_step, 4.0f, 40.0f, "HP", 1.0f );
						slider_row( "Render Radius", p.safe_zone_draw_radius, 200.0f, 2500.0f, "u", 10.0f );
					} );
			} );
	}
	else if ( this->m_visual_group == 4 )
	{
		auto& p = config::visual_settings.m_radar;
		card_in_column( "radar_players", "RADAR PLAYERS", 5, 0, [ & ]
			{
				const auto uses_key = p.activation_mode
					!= config::visual_profile::radar::always_on;
				toggle_popup_row( "Overlay Enemy Markers", p.enabled,
					uses_key ? 2 : 1, [ & ]
					{
						int mode = p.activation_mode;
						static constexpr const char* modes[ ]{ "Always On", "Hold", "Toggle" };
						select_row( "Activation", mode, modes );
						p.activation_mode = std::clamp( mode,
							static_cast<int>( config::visual_profile::radar::always_on ),
							static_cast<int>( config::visual_profile::radar::toggle ) );
						if ( p.activation_mode != config::visual_profile::radar::always_on )
							keybind_row( "Key", p.activation_key );
					} );
				toggle_color_row( "Player Names", p.show_names, p.name_color );
				toggle_color_row( "Player Health", p.show_health, p.health_color );
				toggle_color_row( "Player Armor", p.show_armor, p.armor_color );
				toggle_color_row( "Player Weapon", p.show_weapon, p.weapon_color );
			} );
		card_in_column( "radar_style", "PLAYER STYLE", 5, 0, [ & ]
			{
				color_row( "Enemy Marker", p.enemy_color );
				color_row( "View Direction", p.direction_color );
				color_row( "Status Text", p.status_color );
				slider_row( "Marker Scale", p.marker_scale, 0.5f, 2.0f, "", 0.05f );
				toggle_popup_row( "Text Outline", p.text_outline, 2, [ & ]
					{
						color_row( "Outline Color", p.text_outline_color );
						slider_row( "Outline Thickness", p.text_outline_thickness,
							0.5f, 3.0f, " px", 0.5f );
					} );
			} );
		card_in_column( "radar_grenades", "RADAR GRENADES", 3, 1, [ & ]
			{
				toggle_popup_row( "Grenade Markers", p.show_projectiles, 6, [ & ]
					{
						color_row( "HE Color", p.he_color );
						color_row( "Flash Color", p.flash_color );
						color_row( "Smoke Color", p.smoke_color );
						color_row( "Molotov Color", p.molotov_color );
						color_row( "Decoy Color", p.decoy_color );
						slider_row( "Information Scale", p.information_scale,
							0.5f, 1.5f, "", 0.05f );
					} );
				toggle_popup_row( "Grenade Trajectories", p.show_trajectories, 2, [ & ]
					{
						slider_row( "Line Thickness", p.trajectory_thickness,
							0.5f, 6.0f, " px", 0.25f );
						slider_row( "End Point Size", p.trajectory_endpoint_size,
							1.0f, 10.0f, " px", 0.5f );
					} );
				toggle_popup_row( "Grenade Zones", p.show_grenade_zones, 3, [ & ]
					{
						slider_row( "Fill Opacity", p.zone_fill_alpha, 0.0f, 100.0f, "%", 1.0f );
						slider_row( "Outline Opacity", p.zone_outline_alpha, 0.0f, 100.0f, "%", 1.0f );
						slider_row( "Outline Thickness", p.zone_outline_thickness,
							0.5f, 5.0f, " px", 0.25f );
					} );
			} );
	}
	else if ( this->m_visual_group == 5 )
	{
		auto& general = config::general_settings;
		card( "bullet_effects", "BULLET EFFECTS", 3, [ & ]
			{
				auto& tracers = general.m_bullet_tracers;
				toggle_popup_row( "Enable Bullet Tracers", tracers.enabled, 5, [ & ]
					{
						settings_popup_row( "Appearance", 3, [ & ]
							{
								color_row( "Tracer Color", tracers.color );
								slider_row( "Thickness", tracers.thickness, 1.0f, 10.0f, "", 0.25f );
								toggle_row( "Bloom Effect", tracers.bloom );
							} );
						settings_popup_row( "Lifetime", 2, [ & ]
							{
								slider_row( "Fade Duration", tracers.duration, 1.0f, 10.0f, "s", 0.1f );
								slider_row( "Max Tracers", tracers.max_count, 1, 100 );
							} );
						toggle_row( "Draw Trajectory Line", tracers.draw_line );
						toggle_popup_row( "Impact Cubes", tracers.draw_cubes, 3, [ & ]
							{
								slider_row( "Cube Size", tracers.cube_half, 0.5f, 5.0f, "", 0.25f );
								color_row( "Cube Edge Color", tracers.cube_edge_color );
								slider_row( "Cube Face Alpha", tracers.cube_face_alpha, 0.0f, 160.0f, "", 1.0f );
							} );
						settings_popup_row( "Distance Fade", 2, [ & ]
							{
								slider_row( "Fade Near", tracers.fade_near, 5.0f, 200.0f, "u", 1.0f );
								slider_row( "Fade Far", tracers.fade_far, 50.0f, 500.0f, "u", 1.0f );
							} );
					} );

				auto& marker = general.m_hitmarker;
				toggle_popup_row( "World Hitmarker", marker.enabled, 3, [ & ]
					{
						color_row( "Marker Color", marker.color );
						settings_popup_row( "Geometry", 3, [ & ]
							{
								slider_row( "Marker Size", marker.size, 3.0f, 18.0f, "", 0.5f );
								slider_row( "Center Gap", marker.gap, 0.0f, 10.0f, "", 0.5f );
								slider_row( "Marker Thickness", marker.thickness, 1.0f, 4.0f, "", 0.25f );
							} );
						slider_row( "Marker Duration", marker.duration, 0.15f, 1.5f, "s", 0.05f );
					} );

				static constexpr std::array<const char*, 5> hitsound_styles{
					"Soft", "Glass", "Pluck", "Crisp", "Flesh" };
				auto& sound = general.m_hitsound;
				toggle_popup_row( "Hit Sound", sound.enabled, 4, [ & ]
					{
						select_row( "Sound", sound.style, hitsound_styles );
						slider_percent_row( "Volume", sound.volume );
						if ( button_row( "Preview", "Play" ) )
							features::visuals::hitsounds().play( sound.style, sound.volume );
						toggle_popup_row( "Floating Damage", sound.show_damage, 4, [ & ]
							{
								color_row( "Damage Color", sound.damage_color );
								slider_row( "Damage Size", sound.damage_size, 8.0f, 28.0f, "", 0.5f );
								slider_row( "Damage Duration", sound.damage_duration, 0.15f, 2.0f, "s", 0.05f );
								slider_row( "Damage Rise", sound.damage_rise, 0.0f, 100.0f, "px", 1.0f );
							} );
					} );
			} );

		auto& no_flash = config::visual_settings.m_no_flash;
		card( "no_flash", "NO FLASH (WIREFRAME)", 1, [ & ]
			{
				toggle_popup_row( "Enable Visual No Flash", no_flash.enabled, 3, [ & ]
					{
						slider_row( "Render Distance", no_flash.max_distance, 200.0f, 3000.0f, "", 10.0f );
						color_row( "Dimming Overlay Color", no_flash.background_color );
						color_row( "Wireframe Color", no_flash.wireframe_color );
					} );
			} );
	}
	else
	{
		auto& p = config::visual_settings.m_crosshair;
		card( "crosshair_settings", "CROSSHAIR SETTINGS", 5, [ & ]
			{
				toggle_row( "Enable Crosshair", p.enabled );
				toggle_row( "Copy Game Crosshair", p.copy_game );
				toggle_row( "Draw Dot", p.dot );
				toggle_popup_row( "Draw Lines", p.lines, 3, [ & ]
					{
						toggle_row( "T-Style", p.t_style );
						slider_row( "Length", p.length, 1.0f, 50.0f, "", 0.5f );
						slider_row( "Gap", p.gap, 0.0f, 50.0f, "", 0.5f );
					} );
				slider_row( "Thickness", p.thickness, 1.0f, 10.0f, "", 0.25f );
			} );
		card( "crosshair_colors", "COLORS", 3, [ & ]
			{
				toggle_popup_row( "Draw Outline", p.outline, 2, [ & ]
					{
						color_row( "Outline Color", p.outline_color );
						slider_row( "Outline Thickness", p.outline_thickness,
							0.5f, 3.0f, "", 0.5f );
					} );
				color_row( "Primary Color", p.color );
				toggle_popup_row( "Penetration Indicator", p.penetration_enabled, 3, [ & ]
					{
						color_row( "Can Penetrate", p.penetration_color_yes );
						color_row( "Cannot Penetrate", p.penetration_color_no );
						slider_row( "Min Damage", p.penetration_min_damage, 1.0f, 200.0f, "", 1.0f );
					} );
			} );
	}

	end_cards( );
	ImGui::EndChild( );
	ImGui::PopStyleVar( );
}

void menu_t::draw_misc( )
{
	static constexpr const char* tabs[ ]{ "General", "Grenades", "Movement", "Overlay", "Lua API", "Configs" };
	ImGui::SetCursorPos( { 24.0f, 24.0f } );
	for ( int i = 0; i < 6; ++i )
	{
		if ( i ) ImGui::SameLine( 0.0f, 10.0f );
		if ( tab_button( tabs[ i ], this->m_misc_group == i ) && this->m_misc_group != i )
		{
			this->m_misc_group = i;
			this->reset_content_animation( );
		}
	}

	ImGui::SetCursorPos( { 14.0f, 68.0f } );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, { 0.0f, 0.0f } );
	ImGui::BeginChild( "##misc_cards", { 682.0f, 568.0f }, false );
	begin_cards( "##misc_grid" );
	auto& p = config::general_settings;

	if ( this->m_misc_group == 0 )
	{
		card_in_column( "interface", "INTERFACE", 3, 0, [ & ]
		{
			static constexpr const char* languages[]{ "English", "Русский" };
			const int previous = p.language;
			select_row( "Language", p.language, languages );
			if ( p.language != previous )
			{
				p.language = std::clamp( p.language, 0,
					static_cast<int>( render::localization::id::count ) - 1 );
				render::localization::set( static_cast<render::localization::id>( p.language ) );
			}
			static constexpr std::array dpi_scales{ 0.50f, 0.75f, 1.00f, 1.25f, 1.50f };
			static constexpr const char* dpi_labels[]{ "50%", "75%", "100%", "125%", "150%" };
			const auto closest_scale = std::min_element( dpi_scales.begin(), dpi_scales.end(),
				[ & ]( const float left, const float right )
				{ return std::abs( left - p.menu_scale ) < std::abs( right - p.menu_scale ); } );
			int dpi_index = static_cast<int>( std::distance( dpi_scales.begin(), closest_scale ) );
			select_row( "DPI Scale", dpi_index, dpi_labels );
			p.menu_scale = dpi_scales[ std::clamp( dpi_index, 0,
				static_cast<int>( dpi_scales.size() ) - 1 ) ];
			settings_popup_row( "Interface Colors", 3, [ & ]
			{
				settings_popup_row( "Typography", 2, [ & ]
				{
					color_row( "Primary Text", p.palette.text );
					color_row( "Muted Text", p.palette.muted_text );
				} );
				settings_popup_row( "Surfaces", 4, [ & ]
				{
					color_row( "Menu Background", p.palette.background );
					color_row( "Controls", p.palette.panel );
					color_row( "Containers", p.palette.card );
					color_row( "Popups", p.palette.popup );
				} );
				settings_popup_row( "Interaction", 3, [ & ]
				{
					color_row( "Accent", p.palette.accent );
					color_row( "Hover", p.palette.hover );
					color_row( "Borders", p.palette.border );
				} );
			} );
		} );
		card_in_column( "automation", "AUTOMATION", 1, 1, [ & ]
			{ toggle_row( "Auto Accept Match", p.auto_accept ); } );
		card_in_column( "engine", "ENGINE", 1, 1, [ & ]
		{
			toggle_popup_row( "FPS Limiter", p.limit_fps, 1, [ & ]
			{
				slider_row( "Maximum FPS", p.fps_limit, 30, 1000 );
			} );
		} );
	}
	else if ( this->m_misc_group == 1 )
	{
		auto& n = p.m_nade_helper;
		auto& assist = config::combat_settings.global.grenade_aim;
		card_in_column( "lineups", "LINEUP HELPER", 2, 0, [ & ]
		{
			toggle_popup_row( "Lineup Helper", n.enabled, 4, [ & ]
			{
				settings_popup_row( "Geometry", 5, [ & ]
				{
					slider_row( "Draw Distance", n.draw_distance, 200.0f, 4000.0f, "u", 10.0f );
					slider_row( "Marker Distance", n.stand_distance, 60.0f, 800.0f, "u", 5.0f );
					slider_row( "Stand Radius", n.stand_radius, 6.0f, 64.0f, "u", 1.0f );
					slider_row( "Release Radius", n.release_radius, 1.0f, 16.0f, "u", 0.5f );
					slider_row( "Height Tolerance", n.height_tolerance, 1.0f, 24.0f, "u", 0.5f );
				} );
				settings_popup_row( "Display", 2, [ & ]
				{
					toggle_row( "Show Throw Type", n.show_action );
					toggle_row( "Show Distance", n.show_distance );
				} );
				settings_popup_row( "Plaque Style", 3, [ & ]
				{
					color_row( "Background", n.plaque_background );
					color_row( "Text", n.plaque_text );
					color_row( "Accent", n.plaque_accent );
				} );
				settings_popup_row( "Marker Style", 3, [ & ]
				{
					color_row( "Stand Marker", n.stand_marker );
					color_row( "Stand Marker Active", n.stand_marker_active );
					color_row( "Aim Marker", n.aim_marker );
				} );
			} );
			toggle_popup_row( "Lineup Aim Assist", n.aim_assist, 3, [ & ]
			{
				keybind_row( "Aim Key", n.aim_key );
				toggle_row( "Auto Release", n.auto_release );
				settings_popup_row( "Aim Tuning", 3, [ & ]
				{
					slider_row( "Smoothing", n.aim_smoothing, 1, 30 );
					slider_row( "Lock Threshold", n.aim_threshold, 0.05f, 3.0f, "deg", 0.05f );
					slider_row( "Settle Time", n.lock_time_ms, 0, 250, "ms" );
				} );
			} );
		} );

		card_in_column( "grenade_assist", "GRENADE ASSIST", 1, 1, [ & ]
		{
			toggle_popup_row( "Enemy Aim Assist", assist.enabled, 2, [ & ]
			{
				keybind_row( "Activation Key", assist.key );
				settings_popup_row( "Aim Tuning", 2, [ & ]
				{
					slider_row( "Target FOV", assist.fov, 1, 180 );
					slider_row( "Smoothing", assist.smoothing, 1, 50 );
				} );
			} );
		} );

		card_in_column( "trajectory", "TRAJECTORY", 1, 1, [ & ]
		{
			toggle_popup_row( "Trajectory Preview", p.m_grenades.enabled, 4, [ & ]
			{
				toggle_row( "Local Prediction Only", p.m_grenades.local_only );
				settings_popup_row( "Line Style", 3, [ & ]
				{
					color_row( "Line Color", p.m_grenades.color );
					slider_row( "Line Thickness", p.m_grenades.thickness,
						0.5f, 8.0f, "px", 0.25f );
					toggle_popup_row( "Bloom", p.m_grenades.bloom, 2, [ & ]
					{
						color_row( "Bloom Color", p.m_grenades.bloom_color );
						slider_row( "Bloom Radius", p.m_grenades.bloom_radius,
							0.5f, 12.0f, "px", 0.5f );
					} );
				} );
				toggle_popup_row( "Bounce Points", p.m_grenades.show_bounces, 2, [ & ]
				{
					color_row( "Point Color", p.m_grenades.bounce_color );
					slider_row( "Point Size", p.m_grenades.bounce_size,
						1.0f, 16.0f, "px", 0.5f );
				} );
				toggle_popup_row( "End Point", p.m_grenades.show_endpoint, 2, [ & ]
				{
					color_row( "Point Color", p.m_grenades.endpoint_color );
					slider_row( "Point Size", p.m_grenades.endpoint_size,
						2.0f, 24.0f, "px", 0.5f );
				} );
			} );
		} );
	}
	else if ( this->m_misc_group == 2 )
	{
		card( "movement", "MOVEMENT", 3, [ & ]
		{
			toggle_popup_row( "Enable Bunny Hop", p.m_bunny_hop.enabled, 1, [ & ]
			{
				keybind_row( "Activation Key", p.m_bunny_hop.activation_key );
			} );
			toggle_popup_row( "Enable Edge Jump", p.m_edge_jump.enabled, 1, [ & ]
			{
				keybind_row( "Activation Key", p.m_edge_jump.activation_key );
			} );
			toggle_popup_row( "Enable Auto Stop", p.m_auto_stop.enabled, 2, [ & ]
			{
				slider_row( "Stop Speed", p.m_auto_stop.stop_speed,
					0.0f, 150.0f, " u/s", 1.0f );
				slider_row( "Shoot Speed", p.m_auto_stop.required_shoot_speed,
					0.0f, 60.0f, "%", 1.0f );
			} );
		} );
	}
	else if ( this->m_misc_group == 3 )
	{
		card( "overlay", "OVERLAY", 6, [ & ]
		{
			toggle_row( "OBS Bypass", p.obs_bypass );
			toggle_popup_row( "Show Watermark", p.m_watermark.enabled, 4, [ & ]
			{
				toggle_row( "Show Ping", p.m_watermark.show_ping );
				toggle_row( "Show Loss", p.m_watermark.show_loss );
				toggle_row( "Show CPU Load", p.m_watermark.show_cpu );
				toggle_row( "Show FPS", p.m_watermark.show_fps );
			} );
			toggle_row( "Show Spectators", p.m_spectator_list.enabled );
			toggle_popup_row( "Show Event Log", p.m_event_log.enabled, 2, [ & ]
			{
				slider_row( "Duration", p.m_event_log.duration,
					0.5f, 20.0f, " s", 0.5f );
				slider_row( "Maximum Entries", p.m_event_log.max_entries, 1, 5 );
			} );
			toggle_row( "Show Active Binds", p.m_keybind_list.enabled );
			toggle_row( "Show Bomb Info",
				config::visual_settings.m_bomb.show_info_panel );
		} );
	}
	else if ( this->m_misc_group == 4 )
	{
		const auto scripts = scripting::runtime().scripts( );
		card_in_column( "lua_runtime", "LUA RUNTIME", 3, 0, [ & ]
		{
			const auto previous = p.lua_enabled;
			toggle_row( "Enable Lua API", p.lua_enabled );
			if ( p.lua_enabled != previous && p.lua_enabled )
				for ( const auto& script : scripts ) if ( script.autoload )
					scripting::runtime().set_enabled( script.id, true );
			if ( button_row( "Script Directory", "Open Folder", row_action_icon::folder ) )
			{
				const auto path = scripting::runtime().scripts_path().u8string( );
				auto& platform = ImGui::GetPlatformIO( );
				if ( platform.Platform_OpenInShellFn ) platform.Platform_OpenInShellFn(
					ImGui::GetCurrentContext( ), reinterpret_cast<const char*>( path.c_str( ) ) );
			}
			if ( button_row( "Load Lua Script", "Browse...", row_action_icon::folder ) )
				app::context().overlay.request_lua_import( );
		} );

		int script_rows = std::max( 1, static_cast<int>( scripts.size( ) ) );
		for ( const auto& script : scripts )
			if ( _stricmp( script.id.c_str( ), "vesta_web_radar" ) == 0
				&& script.enabled ) ++script_rows;
		card_in_column( "lua_scripts", "SCRIPTS",
			script_rows, 1, [ & ]
		{
			if ( scripts.empty( ) )
			{
				begin_row( "No Lua scripts found", 148.0f );
				clipped_row_text( render::localization::tr( "Copy scripts into the Lua directory" ) );
				end_row( );
			}
			for ( const auto& script : scripts )
			{
				ImGui::PushID( script.id.c_str( ) );
				auto running = script.enabled;
				const auto controls = scripting::runtime().controls( script.id );
				if ( _stricmp( script.id.c_str( ), "vesta_web_radar" ) == 0 )
				{
					toggle_row( script.name.c_str( ), running );
					if ( running )
					{
						for ( const auto& control : controls )
						{
							if ( control.id != "copy" || control.kind != scripting::control_kind::button )
								continue;
							if ( button_row( "Copy link", control.action_text.empty( )
								? "Copy" : control.action_text.c_str( ) ) )
								scripting::runtime().press_control( script.id, control.id );
						}
					}
					if ( running != script.enabled )
						scripting::runtime().set_enabled( script.id, running );
					ImGui::PopID( );
					continue;
				}
				const auto popup_rows = std::clamp( 5 + static_cast<int>( controls.size( ) ), 5, 16 );
				toggle_popup_row( script.name.c_str( ), running, popup_rows, [ & ]
				{
					auto autoload = script.autoload;
					toggle_row( "Autoload", autoload );
					if ( autoload != script.autoload ) scripting::runtime().set_autoload( script.id, autoload );
					auto hot_reload = script.hot_reload;
					toggle_row( "Hot Reload", hot_reload );
					if ( hot_reload != script.hot_reload ) scripting::runtime().set_hot_reload( script.id, hot_reload );

					begin_row( "Status", 116.0f );
					ImGui::TextColored( script.state == scripting::script_state::running
						? ImVec4{ 0.38f, 0.86f, 0.55f, 1.0f }
						: ( script.state == scripting::script_state::error
							|| script.state == scripting::script_state::over_budget )
							? ImVec4{ 1.0f, 0.38f, 0.42f, 1.0f } : k_text_muted,
						"%s", scripting::runtime_t::state_name( script.state ) );
					end_row( );
					begin_row( "Runtime", 116.0f );
					ImGui::TextColored( k_text_muted, "%.2f ms / %.1f MiB",
						script.last_callback_ms,
						static_cast<double>( script.memory_bytes ) / ( 1024.0 * 1024.0 ) );
					end_row( );
					if ( !script.error.empty( ) )
					{
						begin_row( "Last Error", 116.0f );
						clipped_row_text( script.error, ImVec4{ 1.0f, 0.38f, 0.42f, 1.0f } );
						end_row( );
					}

					for ( const auto& control : controls )
					{
						ImGui::PushID( control.id.c_str( ) );
						switch ( control.kind )
						{
						case scripting::control_kind::text:
							begin_row( control.label.c_str( ), 116.0f );
							if ( const auto* value = std::get_if<std::string>( &control.value ) ) clipped_row_text( *value );
							end_row( );
							break;
						case scripting::control_kind::button:
							if ( button_row( control.label.c_str( ), control.action_text.empty( ) ? "Run" : control.action_text.c_str( ) ) )
								scripting::runtime().press_control( script.id, control.id );
							break;
						case scripting::control_kind::toggle:
						{
							auto value = std::get_if<bool>( &control.value ) ? std::get<bool>( control.value ) : false;
							const auto before = value; toggle_row( control.label.c_str( ), value );
							if ( value != before ) scripting::runtime().set_control( script.id, control.id, value );
							break;
						}
						case scripting::control_kind::slider:
						{
							auto value = static_cast<float>( std::get_if<double>( &control.value ) ? std::get<double>( control.value ) : 0.0 );
							const auto before = value;
							slider_row( control.label.c_str( ), value, static_cast<float>( control.minimum ),
								static_cast<float>( control.maximum ), "", static_cast<float>( control.step ) );
							if ( value != before ) scripting::runtime().set_control( script.id, control.id, static_cast<double>( value ) );
							break;
						}
						case scripting::control_kind::select:
						{
							auto value = std::get_if<int>( &control.value ) ? std::get<int>( control.value ) : 0;
							std::vector<const char*> labels{}; labels.reserve( control.options.size( ) );
							for ( const auto& option : control.options ) labels.push_back( option.c_str( ) );
							const auto before = value; select_row( control.label.c_str( ), value, labels );
							if ( value != before ) scripting::runtime().set_control( script.id, control.id, value );
							break;
						}
						case scripting::control_kind::input:
						{
							std::array<char, 512> buffer{};
							if ( const auto* value = std::get_if<std::string>( &control.value ) )
								std::memcpy( buffer.data( ), value->data( ),
									std::min( value->size( ), buffer.size( ) - 1 ) );
							const auto before = std::string( buffer.data( ) );
							text_input_row( control.label.c_str( ), buffer.data( ), buffer.size( ) );
							if ( before != buffer.data( ) ) scripting::runtime().set_control(
								script.id, control.id, std::string( buffer.data( ) ) );
							break;
						}
						case scripting::control_kind::color:
						{
							auto value = std::get_if<zdraw::rgba>( &control.value )
								? std::get<zdraw::rgba>( control.value ) : zdraw::rgba{ 255, 255, 255, 255 };
							const auto before = value.val; color_row( control.label.c_str( ), value );
							if ( before != value.val ) scripting::runtime().set_control( script.id, control.id, value );
							break;
						}
						case scripting::control_kind::keybind:
						{
							auto value = std::get_if<int>( &control.value ) ? std::get<int>( control.value ) : 0;
							const auto before = value; keybind_row( control.label.c_str( ), value );
							if ( before != value ) scripting::runtime().set_control( script.id, control.id, value );
							break;
						}
						case scripting::control_kind::separator:
							begin_row( control.label.c_str( ), 116.0f );
							ImGui::SeparatorText( control.label.c_str( ) );
							end_row( );
							break;
						}
						ImGui::PopID( );
					}
					if ( button_row( "Reload Script", "Reload" ) ) scripting::runtime().reload( script.id );
				} );
				if ( running != script.enabled ) scripting::runtime().set_enabled( script.id, running );
				ImGui::PopID( );
			}
		} );
	}
	else
	{
		card( "config", "CFG FILE", 3, [ & ]
			{
				auto& cfg = config::storage;
				text_input_row( "File Name", cfg.name_buffer, sizeof( cfg.name_buffer ) );
				if ( button_row( "Export Named CFG", "Save As...", row_action_icon::save ) )
					app::context().overlay.request_config_save( );
				if ( button_row( "Open CFG", "Browse...", row_action_icon::folder ) )
					app::context().overlay.request_config_load( );
			} );
	}

	end_cards( );
	ImGui::EndChild( );
	ImGui::PopStyleVar( );
}
