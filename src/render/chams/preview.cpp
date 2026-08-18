#include <stdafx.hpp>
#include <render/chams/preview.hpp>
#include <render/chams/renderer.hpp>
#include <render/chams/mesh_cache.hpp>

#include <d3dcompiler.h>

namespace chams {

	namespace {

		struct cb_scene
		{
			float view_projection[ 4 ][ 4 ];
			float eye[ 3 ];
			float pad0;
			float tint[ 4 ];
			float uv_scale[ 2 ];
			float uv_offset[ 2 ];
			int has_normal;
			int has_metalness;
			int has_ao;
			int has_gloss;
		};
		static_assert( sizeof( cb_scene ) % 16 == 0 );

		constexpr const char* k_preview_shader = R"(
			cbuffer CBScene : register(b0)
			{
				row_major float4x4 g_ViewProjection;
				float3 g_EyePos;
				float  g_Pad0;
				float4 g_Tint;
				float2 g_UvScale;
				float2 g_UvOffset;
				int    g_HasNormal;
				int    g_HasMetalness;
				int    g_HasAO;
				int    g_HasGloss;
			};

			Texture2D    g_Color      : register(t0);
			Texture2D    g_Normal     : register(t1);
			Texture2D    g_Metalness  : register(t2);
			Texture2D    g_AO         : register(t3);
			Texture2D    g_Gloss      : register(t4);
			SamplerState g_Sampler    : register(s0);

			cbuffer CBBones : register(b1)
			{
				row_major float4x4 g_Bones[128];
			};

			struct VS_INPUT
			{
				float3 Position    : POSITION;
				float3 Normal      : NORMAL;
				float4 Tangent     : TANGENT;
				float2 UV          : TEXCOORD0;
				uint4  BoneIndices : BLENDINDICES;
				float4 BoneWeights : BLENDWEIGHT;
			};

			struct PS_INPUT
			{
				float4 Position : SV_POSITION;
				float3 WorldPos : TEXCOORD0;
				float3 Normal   : TEXCOORD1;
				float3 Tangent  : TEXCOORD2;
				float3 Bitangent: TEXCOORD3;
				float2 UV       : TEXCOORD4;
			};

			PS_INPUT VS_Main(VS_INPUT input)
			{
				PS_INPUT output;
				float4 skinned = float4(0, 0, 0, 0);
				float3 normal  = float3(0, 0, 0);
				float3 tangent = float3(0, 0, 0);

				[unroll]
				for (int i = 0; i < 4; i++)
				{
					float weight = input.BoneWeights[i];
					if (weight > 0.0001f)
					{
						float4x4 bone = g_Bones[input.BoneIndices[i]];
						skinned += mul(bone, float4(input.Position, 1.0f)) * weight;
						normal  += mul((float3x3)bone, input.Normal) * weight;
						tangent += mul((float3x3)bone, input.Tangent.xyz) * weight;
					}
				}
				skinned.w = 1.0f;

				output.Position  = mul(g_ViewProjection, skinned);
				output.WorldPos  = skinned.xyz;
				output.Normal    = normal;
				output.Tangent   = tangent;
				output.Bitangent = cross(normal, tangent) * input.Tangent.w;
				output.UV        = input.UV * g_UvScale + g_UvOffset;
				return output;
			}

			float4 PS_Main(PS_INPUT input) : SV_TARGET
			{
				float4 albedo = g_Color.Sample(g_Sampler, input.UV) * g_Tint;

				float3 N = normalize(input.Normal);
				if (g_HasNormal != 0)
				{

					float2 packed = g_Normal.Sample(g_Sampler, input.UV).xy * 2.0f - 1.0f;
					float3 tangentNormal = float3(packed, sqrt(saturate(1.0f - dot(packed, packed))));
					float3x3 tbn = float3x3(normalize(input.Tangent), normalize(input.Bitangent), N);
					N = normalize(mul(tangentNormal, tbn));
				}

				float metalness = g_HasMetalness != 0 ? g_Metalness.Sample(g_Sampler, input.UV).r : 0.0f;
				float ao        = g_HasAO        != 0 ? g_AO.Sample(g_Sampler, input.UV).r : 1.0f;

				float roughness = 0.82f;
				if (g_HasGloss != 0)
				{
					roughness = saturate(1.0f - g_Gloss.Sample(g_Sampler, input.UV).r);
				}

				float3 V = normalize(g_EyePos - input.WorldPos);
				N = dot(N, V) < 0.0f ? -N : N;

				float3 key  = normalize(V + float3( 0.55f,  0.45f, 0.55f));
				float3 fill = normalize(V + float3(-0.75f, -0.25f, 0.10f));

				float wrap_key  = saturate(dot(N, key)  * 0.5f + 0.5f);
				float wrap_fill = saturate(dot(N, fill) * 0.5f + 0.5f);
				float rim       = pow(1.0f - saturate(dot(N, V)), 2.5f);

				const float3 key_color  = float3(1.00f, 0.93f, 0.82f);
				const float3 fill_color = float3(0.72f, 0.80f, 0.95f);
				const float3 rim_color  = float3(1.00f, 0.88f, 0.72f);

				float  sky = saturate(N.z * 0.5f + 0.5f);
				float3 ambient = lerp(float3(0.16f, 0.17f, 0.20f), float3(0.30f, 0.30f, 0.31f), sky);

				float3 light = ambient
					+ key_color  * (wrap_key  * wrap_key * 0.85f)
					+ fill_color * (wrap_fill * 0.28f);

				float3 diffuse = albedo.rgb * light * ao;

				float3 H = normalize(key + V);
				float gloss_exp = lerp(64.0f, 4.0f, roughness);
				float spec = pow(saturate(dot(N, H)), gloss_exp) * (1.0f - roughness) * 0.5f;
				float3 specColor = lerp(float3(0.25f, 0.25f, 0.25f), albedo.rgb, metalness);

				float3 rgb = diffuse + specColor * spec + rim_color * albedo.rgb * rim * 0.18f;
				return float4(rgb, 1.0f);
			}
)";

		void build_view_projection( const foundation::vec3& eye, const foundation::vec3& target,
			float fov_radians, float aspect, float near_z, float far_z, float out[ 4 ][ 4 ] )
		{

			auto forward = target - eye;
			const auto forward_len = forward.length( );
			if ( forward_len > 1e-6f )
			{
				forward /= forward_len;
			}

			const foundation::vec3 world_up{ 0.0f, 0.0f, 1.0f };
			auto right = forward.cross( world_up );
			const auto right_len = right.length( );
			right = right_len > 1e-6f ? right / right_len : foundation::vec3{ 0.0f, 1.0f, 0.0f };
			auto up = right.cross( forward );

			const auto h = 1.0f / std::tan( fov_radians * 0.5f );
			const auto w = h / aspect;
			const auto q = far_z / ( far_z - near_z );

			const float view[ 3 ][ 3 ]{
				{ right.x, right.y, right.z },
				{ up.x, up.y, up.z },
				{ forward.x, forward.y, forward.z } };

			const float t[ 3 ]{
				-( view[ 0 ][ 0 ] * eye.x + view[ 0 ][ 1 ] * eye.y + view[ 0 ][ 2 ] * eye.z ),
				-( view[ 1 ][ 0 ] * eye.x + view[ 1 ][ 1 ] * eye.y + view[ 1 ][ 2 ] * eye.z ),
				-( view[ 2 ][ 0 ] * eye.x + view[ 2 ][ 1 ] * eye.y + view[ 2 ][ 2 ] * eye.z ) };

			for ( int c = 0; c < 3; ++c )
			{
				out[ 0 ][ c ] = view[ 0 ][ c ] * w;
				out[ 1 ][ c ] = view[ 1 ][ c ] * h;
				out[ 2 ][ c ] = view[ 2 ][ c ] * q;
				out[ 3 ][ c ] = view[ 2 ][ c ];
			}

			out[ 0 ][ 3 ] = t[ 0 ] * w;
			out[ 1 ][ 3 ] = t[ 1 ] * h;
			out[ 2 ][ 3 ] = ( t[ 2 ] - near_z ) * q;
			out[ 3 ][ 3 ] = t[ 2 ];
		}

	}

	bool preview::initialize( ID3D11Device* device, ID3D11DeviceContext* context )
	{
		this->m_device = device;
		this->m_context = context;

		if ( !device || !context || !this->create_shaders( ) || !this->create_states( ) )
		{
			return false;
		}

		this->m_ready = true;
		return true;
	}

	bool preview::create_shaders( )
	{
		ID3DBlob* vs_blob{};
		ID3DBlob* ps_blob{};
		ID3DBlob* error_blob{};

		auto hr = D3DCompile( k_preview_shader, std::strlen( k_preview_shader ), nullptr, nullptr, nullptr,
			"VS_Main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vs_blob, &error_blob );
		if ( FAILED( hr ) )
		{
			if ( error_blob )
			{
				app::context().diagnostics.warning( "[preview] VS compile error: {}", static_cast< const char* >( error_blob->GetBufferPointer( ) ) );
				error_blob->Release( );
			}
			return false;
		}

		hr = D3DCompile( k_preview_shader, std::strlen( k_preview_shader ), nullptr, nullptr, nullptr,
			"PS_Main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps_blob, &error_blob );
		if ( FAILED( hr ) )
		{
			if ( error_blob )
			{
				app::context().diagnostics.warning( "[preview] PS compile error: {}", static_cast< const char* >( error_blob->GetBufferPointer( ) ) );
				error_blob->Release( );
			}
			vs_blob->Release( );
			return false;
		}

		this->m_device->CreateVertexShader( vs_blob->GetBufferPointer( ), vs_blob->GetBufferSize( ), nullptr, &this->m_vertex_shader );
		this->m_device->CreatePixelShader( ps_blob->GetBufferPointer( ), ps_blob->GetBufferSize( ), nullptr, &this->m_pixel_shader );

		const D3D11_INPUT_ELEMENT_DESC layout[]
		{
			{ "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TANGENT",      0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT,      0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "BLENDWEIGHT",  0, DXGI_FORMAT_R8G8B8A8_UNORM,     0, 52, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};

		hr = this->m_device->CreateInputLayout( layout, 6, vs_blob->GetBufferPointer( ), vs_blob->GetBufferSize( ), &this->m_input_layout );

		vs_blob->Release( );
		ps_blob->Release( );

		D3D11_BUFFER_DESC cb{};
		cb.ByteWidth = sizeof( cb_scene );
		cb.Usage = D3D11_USAGE_DYNAMIC;
		cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if ( FAILED( this->m_device->CreateBuffer( &cb, nullptr, &this->m_cb_scene ) ) )
		{
			return false;
		}

		cb.ByteWidth = sizeof( float ) * 16 * 128;
		if ( FAILED( this->m_device->CreateBuffer( &cb, nullptr, &this->m_cb_bones ) ) )
		{
			return false;
		}

		return SUCCEEDED( hr ) && this->m_vertex_shader && this->m_pixel_shader;
	}

	bool preview::create_states( )
	{
		D3D11_SAMPLER_DESC sd{};
		sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		sd.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
		sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
		sd.MaxLOD = D3D11_FLOAT32_MAX;
		if ( FAILED( this->m_device->CreateSamplerState( &sd, &this->m_sampler ) ) ) return false;

		D3D11_RASTERIZER_DESC rs{};
		rs.FillMode = D3D11_FILL_SOLID;

		rs.CullMode = D3D11_CULL_NONE;
		rs.DepthClipEnable = TRUE;
		if ( FAILED( this->m_device->CreateRasterizerState( &rs, &this->m_rasterizer ) ) ) return false;

		D3D11_DEPTH_STENCIL_DESC ds{};
		ds.DepthEnable = TRUE;
		ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		ds.DepthFunc = D3D11_COMPARISON_LESS;
		if ( FAILED( this->m_device->CreateDepthStencilState( &ds, &this->m_depth_state ) ) ) return false;

		D3D11_BLEND_DESC bs{};
		bs.RenderTarget[ 0 ].BlendEnable = FALSE;
		bs.RenderTarget[ 0 ].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		if ( FAILED( this->m_device->CreateBlendState( &bs, &this->m_blend_state ) ) ) return false;

		return true;
	}

	bool preview::ensure_target( std::uint32_t width, std::uint32_t height )
	{
		if ( this->m_rtv && width == this->m_width && height == this->m_height )
		{
			return true;
		}

		this->release_target( );

		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

		if ( FAILED( this->m_device->CreateTexture2D( &desc, nullptr, &this->m_color_texture ) ) ) return false;
		if ( FAILED( this->m_device->CreateRenderTargetView( this->m_color_texture, nullptr, &this->m_rtv ) ) ) return false;
		if ( FAILED( this->m_device->CreateShaderResourceView( this->m_color_texture, nullptr, &this->m_srv ) ) ) return false;

		D3D11_TEXTURE2D_DESC depth = desc;
		depth.Format = DXGI_FORMAT_D32_FLOAT;
		depth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		if ( FAILED( this->m_device->CreateTexture2D( &depth, nullptr, &this->m_depth_texture ) ) ) return false;
		if ( FAILED( this->m_device->CreateDepthStencilView( this->m_depth_texture, nullptr, &this->m_dsv ) ) ) return false;

		this->m_msaa_samples = 0;
		UINT samples = 1;
		for ( const UINT candidate : { 8u, 4u, 2u } )
		{
			UINT quality = 0;
			if ( SUCCEEDED( this->m_device->CheckMultisampleQualityLevels( DXGI_FORMAT_R8G8B8A8_UNORM, candidate, &quality ) )
				&& quality > 0 )
			{
				samples = candidate;
				break;
			}
		}
		if ( samples > 1 )
		{
			D3D11_TEXTURE2D_DESC ms = desc;
			ms.SampleDesc.Count = samples;
			ms.BindFlags = D3D11_BIND_RENDER_TARGET;
			D3D11_TEXTURE2D_DESC ms_depth = depth;
			ms_depth.SampleDesc.Count = samples;
			if ( SUCCEEDED( this->m_device->CreateTexture2D( &ms, nullptr, &this->m_msaa_color ) )
				&& SUCCEEDED( this->m_device->CreateRenderTargetView( this->m_msaa_color, nullptr, &this->m_msaa_rtv ) )
				&& SUCCEEDED( this->m_device->CreateTexture2D( &ms_depth, nullptr, &this->m_msaa_depth ) )
				&& SUCCEEDED( this->m_device->CreateDepthStencilView( this->m_msaa_depth, nullptr, &this->m_msaa_dsv ) ) )
			{
				this->m_msaa_samples = samples;
			}
		}

		this->m_width = width;
		this->m_height = height;
		return true;
	}

	void preview::release_target( )
	{
		if ( this->m_dsv ) { this->m_dsv->Release( ); this->m_dsv = nullptr; }
		if ( this->m_depth_texture ) { this->m_depth_texture->Release( ); this->m_depth_texture = nullptr; }
		if ( this->m_srv ) { this->m_srv->Release( ); this->m_srv = nullptr; }
		if ( this->m_rtv ) { this->m_rtv->Release( ); this->m_rtv = nullptr; }
		if ( this->m_color_texture ) { this->m_color_texture->Release( ); this->m_color_texture = nullptr; }
		if ( this->m_msaa_rtv ) { this->m_msaa_rtv->Release( ); this->m_msaa_rtv = nullptr; }
		if ( this->m_msaa_color ) { this->m_msaa_color->Release( ); this->m_msaa_color = nullptr; }
		if ( this->m_msaa_dsv ) { this->m_msaa_dsv->Release( ); this->m_msaa_dsv = nullptr; }
		if ( this->m_msaa_depth ) { this->m_msaa_depth->Release( ); this->m_msaa_depth = nullptr; }
		this->m_msaa_samples = 0;
		this->m_width = 0;
		this->m_height = 0;
	}

	ID3D11ShaderResourceView* preview::get_texture( vpk_archive& vpk, const std::string& path )
	{
		if ( path.empty( ) )
		{
			return nullptr;
		}

		if ( const auto it = this->m_textures.find( path ); it != this->m_textures.end( ) )
		{
			return it->second.srv;
		}

		gpu_texture entry{};
		const auto data = load_texture( vpk, path );

		if ( data.valid( ) )
		{
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = data.width;
			desc.Height = data.height;
			desc.MipLevels = data.mip_count;
			desc.ArraySize = 1;
			desc.Format = static_cast< DXGI_FORMAT >( data.dxgi_format );
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_IMMUTABLE;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

			std::vector<D3D11_SUBRESOURCE_DATA> subresources( data.mip_count );
			for ( std::uint32_t level = 0; level < data.mip_count; ++level )
			{
				const auto w = std::max<std::uint32_t>( 1, data.width >> level );
				const auto blocks_x = std::max<std::uint32_t>( 1, ( w + 3 ) / 4 );

				subresources[ level ].pSysMem = data.mips[ level ].data( );
				subresources[ level ].SysMemPitch = data.block_size != 0
					? blocks_x * data.block_size
					: w * 4;
			}

			if ( SUCCEEDED( this->m_device->CreateTexture2D( &desc, subresources.data( ), &entry.texture ) ) )
			{
				this->m_device->CreateShaderResourceView( entry.texture, nullptr, &entry.srv );
			}
		}

		return this->m_textures.emplace( path, entry ).first->second.srv;
	}

	const preview::gpu_material& preview::get_material( vpk_archive& vpk, const std::string& path )
	{
		if ( const auto it = this->m_materials.find( path ); it != this->m_materials.end( ) )
		{
			return it->second;
		}

		gpu_material entry{};
		const auto data = load_material( vpk, path );
		if ( data.valid( ) )
		{
			entry.color = this->get_texture( vpk, data.color );
			entry.normal = this->get_texture( vpk, data.normal );
			entry.metalness = this->get_texture( vpk, data.metalness );
			entry.ambient_occlusion = this->get_texture( vpk, data.ambient_occlusion );
			entry.gloss = this->get_texture( vpk, data.gloss );
		}

		return this->m_materials.emplace( path, entry ).first->second;
	}

	const preview::gpu_mesh* preview::get_mesh( const std::string& model_path, const skinned_mesh& mesh )
	{
		if ( const auto it = this->m_meshes.find( model_path ); it != this->m_meshes.end( ) )
		{
			return &it->second;
		}

		gpu_mesh gm{};

		D3D11_BUFFER_DESC vb{};
		vb.ByteWidth = static_cast< UINT >( mesh.vertices.size( ) * sizeof( skinned_vertex ) );
		vb.Usage = D3D11_USAGE_IMMUTABLE;
		vb.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		D3D11_SUBRESOURCE_DATA vd{ mesh.vertices.data( ) };
		if ( FAILED( this->m_device->CreateBuffer( &vb, &vd, &gm.vertex_buffer ) ) )
		{
			return nullptr;
		}

		D3D11_BUFFER_DESC ib{};
		ib.ByteWidth = static_cast< UINT >( mesh.indices.size( ) * sizeof( std::uint32_t ) );
		ib.Usage = D3D11_USAGE_IMMUTABLE;
		ib.BindFlags = D3D11_BIND_INDEX_BUFFER;
		D3D11_SUBRESOURCE_DATA id{ mesh.indices.data( ) };
		if ( FAILED( this->m_device->CreateBuffer( &ib, &id, &gm.index_buffer ) ) )
		{
			gm.vertex_buffer->Release( );
			return nullptr;
		}

		return &this->m_meshes.emplace( model_path, gm ).first->second;
	}

	void preview::update_camera( const skinned_mesh& mesh, std::uint32_t width, std::uint32_t height )
	{

		foundation::vec3 mins{ 1e9f, 1e9f, 1e9f };
		foundation::vec3 maxs{ -1e9f, -1e9f, -1e9f };
		for ( const auto& v : mesh.vertices )
		{
			mins.x = std::min( mins.x, v.position[ 0 ] ); maxs.x = std::max( maxs.x, v.position[ 0 ] );
			mins.y = std::min( mins.y, v.position[ 1 ] ); maxs.y = std::max( maxs.y, v.position[ 1 ] );
			mins.z = std::min( mins.z, v.position[ 2 ] ); maxs.z = std::max( maxs.z, v.position[ 2 ] );
		}

		const foundation::vec3 center{ ( mins.x + maxs.x ) * 0.5f, ( mins.y + maxs.y ) * 0.5f, ( mins.z + maxs.z ) * 0.5f };

		constexpr auto k_box_top = 0.035f;
		constexpr auto k_box_bottom = 0.828f;
		constexpr auto k_box_fill = k_box_bottom - k_box_top;
		constexpr auto k_box_center = ( k_box_top + k_box_bottom ) * 0.5f;

		const auto half_height = std::max( ( maxs.z - mins.z ) * 0.5f, 1.0f );

		constexpr auto vertical_fov = 0.7f;
		const auto aspect = static_cast< float >( width ) / static_cast< float >( std::max<std::uint32_t>( height, 1 ) );
		const auto tan_half = std::tan( vertical_fov * 0.5f );

		const auto distance = half_height / ( k_box_fill * tan_half );

		const auto ndc_center = 1.0f - 2.0f * k_box_center;
		const foundation::vec3 target{ center.x, center.y, center.z - ndc_center * distance * tan_half };

		this->m_eye = foundation::vec3{
			target.x + distance * std::cos( this->m_yaw ),
			target.y + distance * std::sin( this->m_yaw ),
			target.z };

		build_view_projection( this->m_eye, target, vertical_fov, aspect,
			std::max( distance * 0.02f, 1.0f ), distance * 4.0f + 512.0f, this->m_view_projection );
	}

	void preview::ensure_idle_clip( vpk_archive& vpk, const std::string& model_path, const skinned_mesh& mesh )
	{
		if ( !this->m_idle_loaded )
		{
			this->m_idle_loaded = true;

			this->m_idle_skeleton = load_nm_skeleton( vpk, "animation/skeletons/characters/worldmodel.vnmskel" );
			this->m_idle_clip = load_nm_clip( vpk,
				"animation/anims/ui_anims/main_menu/ct/ct_main_menu_famas_idle.vnmclip" );
		}

		if ( this->m_idle_mapped_model != model_path )
		{
			this->m_idle_mapped_model = model_path;
			this->m_idle_track_to_bone = this->m_idle_skeleton.valid( )
				? map_tracks_to_model( this->m_idle_skeleton, mesh )
				: std::vector<int>{};
		}
	}

	void preview::orbit( float delta_yaw )
	{
		constexpr auto sensitivity = 0.01f;
		this->m_yaw -= delta_yaw * sensitivity;
	}

	bool preview::bone_position( std::uint32_t bone, foundation::vec3& out ) const
	{
		if ( !this->m_mesh || bone >= this->m_mesh->bones.size( ) )
		{
			return false;
		}

		const auto m = bone < this->m_bone_world.size( )
			? this->m_bone_world[ bone ]
			: this->m_mesh->bones[ bone ].inverse_bind.inverse_rigid( );
		out = foundation::vec3{ m.m[ 0 ][ 3 ], m.m[ 1 ][ 3 ], m.m[ 2 ][ 3 ] };
		return true;
	}

	bool preview::bone_transform( std::uint32_t bone, const foundation::vec3& local, foundation::vec3& out ) const
	{
		if ( !this->m_mesh || bone >= this->m_mesh->bones.size( ) )
		{
			return false;
		}

		const auto m = bone < this->m_bone_world.size( )
			? this->m_bone_world[ bone ]
			: this->m_mesh->bones[ bone ].inverse_bind.inverse_rigid( );
		out = foundation::vec3{
			m.m[ 0 ][ 0 ] * local.x + m.m[ 0 ][ 1 ] * local.y + m.m[ 0 ][ 2 ] * local.z + m.m[ 0 ][ 3 ],
			m.m[ 1 ][ 0 ] * local.x + m.m[ 1 ][ 1 ] * local.y + m.m[ 1 ][ 2 ] * local.z + m.m[ 1 ][ 3 ],
			m.m[ 2 ][ 0 ] * local.x + m.m[ 2 ][ 1 ] * local.y + m.m[ 2 ][ 2 ] * local.z + m.m[ 2 ][ 3 ] };
		return true;
	}

	bool preview::bone_direction( std::uint32_t bone, const foundation::vec3& model_space, foundation::vec3& out ) const
	{
		if ( !this->m_mesh || bone >= this->m_mesh->bones.size( ) )
		{
			return false;
		}

		const auto& to_bone = this->m_mesh->bones[ bone ].inverse_bind;
		const auto from_bone = bone < this->m_bone_world.size( )
			? this->m_bone_world[ bone ]
			: to_bone.inverse_rigid( );

		const float local[ 3 ]{
			to_bone.m[ 0 ][ 0 ] * model_space.x + to_bone.m[ 0 ][ 1 ] * model_space.y + to_bone.m[ 0 ][ 2 ] * model_space.z,
			to_bone.m[ 1 ][ 0 ] * model_space.x + to_bone.m[ 1 ][ 1 ] * model_space.y + to_bone.m[ 1 ][ 2 ] * model_space.z,
			to_bone.m[ 2 ][ 0 ] * model_space.x + to_bone.m[ 2 ][ 1 ] * model_space.y + to_bone.m[ 2 ][ 2 ] * model_space.z };

		out = foundation::vec3{
			from_bone.m[ 0 ][ 0 ] * local[ 0 ] + from_bone.m[ 0 ][ 1 ] * local[ 1 ] + from_bone.m[ 0 ][ 2 ] * local[ 2 ],
			from_bone.m[ 1 ][ 0 ] * local[ 0 ] + from_bone.m[ 1 ][ 1 ] * local[ 1 ] + from_bone.m[ 1 ][ 2 ] * local[ 2 ],
			from_bone.m[ 2 ][ 0 ] * local[ 0 ] + from_bone.m[ 2 ][ 1 ] * local[ 1 ] + from_bone.m[ 2 ][ 2 ] * local[ 2 ] };
		return true;
	}

	bool preview::project( const foundation::vec3& world, float& x, float& y ) const
	{
		const auto& m = this->m_view_projection;
		const auto w = m[ 3 ][ 0 ] * world.x + m[ 3 ][ 1 ] * world.y + m[ 3 ][ 2 ] * world.z + m[ 3 ][ 3 ];
		if ( w < 0.001f )
		{
			return false;
		}

		const auto cx = m[ 0 ][ 0 ] * world.x + m[ 0 ][ 1 ] * world.y + m[ 0 ][ 2 ] * world.z + m[ 0 ][ 3 ];
		const auto cy = m[ 1 ][ 0 ] * world.x + m[ 1 ][ 1 ] * world.y + m[ 1 ][ 2 ] * world.z + m[ 1 ][ 3 ];

		x = ( cx / w * 0.5f + 0.5f ) * static_cast< float >( this->m_width );
		y = ( 0.5f - cy / w * 0.5f ) * static_cast< float >( this->m_height );
		return true;
	}

	ID3D11ShaderResourceView* preview::render( vpk_archive& vpk, const std::string& model_path,
		std::uint32_t width, std::uint32_t height,
		const config::visual_profile::chams::material* chams_material )
	{
		this->m_mesh = nullptr;

		if ( !this->m_ready || model_path.empty( ) || width == 0 || height == 0 )
		{
			return nullptr;
		}

		if ( !this->ensure_target( width, height ) )
		{
			return nullptr;
		}

		const auto& mesh = g_mesh_cache.get_or_build( vpk, model_path );
		if ( !mesh.valid )
		{
			return nullptr;
		}

		const auto* gpu = this->get_mesh( model_path, mesh );
		if ( !gpu )
		{
			return nullptr;
		}

		this->m_mesh = &mesh;
		this->update_camera( mesh, width, height );

		const auto time = static_cast< float >(
			std::chrono::duration_cast< std::chrono::milliseconds >(
				std::chrono::steady_clock::now( ).time_since_epoch( ) ).count( ) % 3600000 ) * 0.001f;
		this->ensure_idle_clip( vpk, model_path, mesh );
		if ( this->m_idle_clip.valid( ) && !this->m_idle_track_to_bone.empty( ) )
		{
			sample_pose( this->m_idle_clip, this->m_idle_skeleton, mesh,
				this->m_idle_track_to_bone, time, this->m_skin_matrices, this->m_bone_world );
		}
		else
		{

			this->m_skin_matrices.assign( mesh.bones.size( ), bone_matrix::identity( ) );
			this->m_bone_world.clear( );
		}

		D3D11_MAPPED_SUBRESOURCE bones_mapped{};
		if ( SUCCEEDED( this->m_context->Map( this->m_cb_bones, 0, D3D11_MAP_WRITE_DISCARD, 0, &bones_mapped ) ) )
		{
			auto* rows = static_cast< float( * )[ 4 ][ 4 ] >( bones_mapped.pData );
			for ( std::size_t i = 0; i < 128; ++i )
		{
				const auto& m = i < this->m_skin_matrices.size( )
					? this->m_skin_matrices[ i ]
					: bone_matrix::identity( );
				for ( int r = 0; r < 3; ++r )
				{
					for ( int c = 0; c < 4; ++c ) rows[ i ][ r ][ c ] = m.m[ r ][ c ];
				}
				rows[ i ][ 3 ][ 0 ] = 0.0f; rows[ i ][ 3 ][ 1 ] = 0.0f;
				rows[ i ][ 3 ][ 2 ] = 0.0f; rows[ i ][ 3 ][ 3 ] = 1.0f;
			}
			this->m_context->Unmap( this->m_cb_bones, 0 );
		}

		ID3D11RenderTargetView* saved_rtv{};
		ID3D11DepthStencilView* saved_dsv{};
		this->m_context->OMGetRenderTargets( 1, &saved_rtv, &saved_dsv );
		UINT saved_viewport_count{ 1 };
		D3D11_VIEWPORT saved_viewport{};
		this->m_context->RSGetViewports( &saved_viewport_count, &saved_viewport );

		const bool use_msaa = config::visual_settings.m_chams.antialiasing && this->m_msaa_samples > 1;
		ID3D11RenderTargetView* const active_rtv = use_msaa ? this->m_msaa_rtv : this->m_rtv;
		ID3D11DepthStencilView* const active_dsv = use_msaa ? this->m_msaa_dsv : this->m_dsv;

		constexpr float clear[ 4 ]{ 0.0f, 0.0f, 0.0f, 0.0f };
		this->m_context->ClearRenderTargetView( active_rtv, clear );
		this->m_context->ClearDepthStencilView( active_dsv, D3D11_CLEAR_DEPTH, 1.0f, 0 );
		this->m_context->OMSetRenderTargets( 1, &active_rtv, active_dsv );

		const D3D11_VIEWPORT viewport{ 0.0f, 0.0f, static_cast< float >( width ), static_cast< float >( height ), 0.0f, 1.0f };
		this->m_context->RSSetViewports( 1, &viewport );

		this->m_context->IASetInputLayout( this->m_input_layout );
		this->m_context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
		this->m_context->VSSetShader( this->m_vertex_shader, nullptr, 0 );
		this->m_context->PSSetShader( this->m_pixel_shader, nullptr, 0 );
		ID3D11Buffer* vs_cbs[]{ this->m_cb_scene, this->m_cb_bones };
		this->m_context->VSSetConstantBuffers( 0, 2, vs_cbs );
		this->m_context->PSSetConstantBuffers( 0, 1, &this->m_cb_scene );
		this->m_context->PSSetSamplers( 0, 1, &this->m_sampler );
		this->m_context->RSSetState( this->m_rasterizer );
		this->m_context->OMSetDepthStencilState( this->m_depth_state, 0 );

		const float blend_factor[ 4 ]{};
		this->m_context->OMSetBlendState( this->m_blend_state, blend_factor, 0xFFFFFFFF );

		const UINT stride = sizeof( skinned_vertex );
		const UINT offset = 0;
		this->m_context->IASetVertexBuffers( 0, 1, &gpu->vertex_buffer, &stride, &offset );
		this->m_context->IASetIndexBuffer( gpu->index_buffer, DXGI_FORMAT_R32_UINT, 0 );

		for ( const auto& dc : mesh.draw_calls )
		{
			const auto& material = this->get_material( vpk, dc.material );
			const auto data = material_data{};

			D3D11_MAPPED_SUBRESOURCE mapped{};
			if ( SUCCEEDED( this->m_context->Map( this->m_cb_scene, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) )
			{
				auto* scene = static_cast< cb_scene* >( mapped.pData );
				std::memcpy( scene->view_projection, this->m_view_projection, sizeof( scene->view_projection ) );
				scene->eye[ 0 ] = this->m_eye.x;
				scene->eye[ 1 ] = this->m_eye.y;
				scene->eye[ 2 ] = this->m_eye.z;
				scene->tint[ 0 ] = scene->tint[ 1 ] = scene->tint[ 2 ] = scene->tint[ 3 ] = 1.0f;
				scene->uv_scale[ 0 ] = 1.0f;
				scene->uv_scale[ 1 ] = 1.0f;
				scene->uv_offset[ 0 ] = 0.0f;
				scene->uv_offset[ 1 ] = 0.0f;
				scene->has_normal = material.normal != nullptr;
				scene->has_metalness = material.metalness != nullptr;
				scene->has_ao = material.ambient_occlusion != nullptr;
				scene->has_gloss = material.gloss != nullptr;
				this->m_context->Unmap( this->m_cb_scene, 0 );
			}

			ID3D11ShaderResourceView* views[ 5 ]{
				material.color, material.normal, material.metalness,
				material.ambient_occlusion, material.gloss };
			this->m_context->PSSetShaderResources( 0, 5, views );

			if ( material.color && !chams_material )
			{
				this->m_context->DrawIndexed( dc.index_count, dc.index_offset, 0 );
			}
		}

		if ( chams_material && chams_material->enabled )
		{
			g_renderer.draw_external( model_path, mesh, *chams_material,
				this->m_skin_matrices, this->m_view_projection, this->m_eye );
		}

		if ( use_msaa )
		{

			this->m_context->OMSetRenderTargets( 0, nullptr, nullptr );
			this->m_context->ResolveSubresource( this->m_color_texture, 0, this->m_msaa_color, 0, DXGI_FORMAT_R8G8B8A8_UNORM );
		}

		this->m_context->OMSetRenderTargets( 1, &saved_rtv, saved_dsv );
		if ( saved_viewport_count > 0 )
		{
			this->m_context->RSSetViewports( 1, &saved_viewport );
		}
		if ( saved_rtv ) saved_rtv->Release( );
		if ( saved_dsv ) saved_dsv->Release( );

		return this->m_srv;
	}

	void preview::shutdown( )
	{
		for ( auto& [ path, texture ] : this->m_textures )
		{
			if ( texture.srv ) texture.srv->Release( );
			if ( texture.texture ) texture.texture->Release( );
		}
		this->m_textures.clear( );
		this->m_materials.clear( );

		for ( auto& [ path, mesh ] : this->m_meshes )
		{
			if ( mesh.vertex_buffer ) mesh.vertex_buffer->Release( );
			if ( mesh.index_buffer ) mesh.index_buffer->Release( );
		}
		this->m_meshes.clear( );

		if ( this->m_vertex_shader ) this->m_vertex_shader->Release( );
		if ( this->m_pixel_shader ) this->m_pixel_shader->Release( );
		if ( this->m_input_layout ) this->m_input_layout->Release( );
		if ( this->m_cb_scene ) this->m_cb_scene->Release( );
		if ( this->m_cb_bones ) this->m_cb_bones->Release( );
		if ( this->m_sampler ) this->m_sampler->Release( );
		if ( this->m_rasterizer ) this->m_rasterizer->Release( );
		if ( this->m_depth_state ) this->m_depth_state->Release( );
		if ( this->m_blend_state ) this->m_blend_state->Release( );

		this->release_target( );
		this->m_ready = false;
	}

}
