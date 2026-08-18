#pragma once

#include <render/chams/material.hpp>
#include <render/chams/mesh.hpp>
#include <render/chams/texture.hpp>
#include <render/chams/animation.hpp>
#include <core/assets/vpk.hpp>

#include <d3d11.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace chams {

	class preview
	{
	public:
		bool initialize( ID3D11Device* device, ID3D11DeviceContext* context );
		void shutdown( );

		[[nodiscard]] ID3D11ShaderResourceView* render( vpk_archive& vpk, const std::string& model_path,
			std::uint32_t width, std::uint32_t height,
			const config::visual_profile::chams::material* chams_material );

		[[nodiscard]] bool bone_position( std::uint32_t bone, foundation::vec3& out ) const;

		[[nodiscard]] bool bone_transform( std::uint32_t bone, const foundation::vec3& local, foundation::vec3& out ) const;

		[[nodiscard]] bool bone_direction( std::uint32_t bone, const foundation::vec3& model_space, foundation::vec3& out ) const;

		[[nodiscard]] bool project( const foundation::vec3& world, float& x, float& y ) const;

		void orbit( float delta_yaw );

		[[nodiscard]] const skinned_mesh* current_mesh( ) const { return this->m_mesh; }

		[[nodiscard]] const float ( &view_projection( ) const )[ 4 ][ 4 ] { return this->m_view_projection; }
		[[nodiscard]] const foundation::vec3& eye( ) const { return this->m_eye; }

	private:
		struct gpu_texture
		{
			ID3D11Texture2D* texture{};
			ID3D11ShaderResourceView* srv{};
		};

		struct gpu_mesh
		{
			ID3D11Buffer* vertex_buffer{};
			ID3D11Buffer* index_buffer{};
		};

		struct gpu_material
		{
			ID3D11ShaderResourceView* color{};
			ID3D11ShaderResourceView* normal{};
			ID3D11ShaderResourceView* metalness{};
			ID3D11ShaderResourceView* ambient_occlusion{};
			ID3D11ShaderResourceView* gloss{};
		};

		bool create_shaders( );
		bool create_states( );
		bool ensure_target( std::uint32_t width, std::uint32_t height );
		void release_target( );

		[[nodiscard]] ID3D11ShaderResourceView* get_texture( vpk_archive& vpk, const std::string& path );
		[[nodiscard]] const gpu_material& get_material( vpk_archive& vpk, const std::string& path );
		[[nodiscard]] const gpu_mesh* get_mesh( const std::string& model_path, const skinned_mesh& mesh );

		void update_camera( const skinned_mesh& mesh, std::uint32_t width, std::uint32_t height );

		void ensure_idle_clip( vpk_archive& vpk, const std::string& model_path, const skinned_mesh& mesh );

		std::vector<bone_matrix> m_skin_matrices{};

		std::vector<bone_matrix> m_bone_world{};

		nm_skeleton m_idle_skeleton{};
		nm_clip m_idle_clip{};
		std::vector<int> m_idle_track_to_bone{};
		std::string m_idle_mapped_model{};
		bool m_idle_loaded{};

		ID3D11Device* m_device{};
		ID3D11DeviceContext* m_context{};

		ID3D11Texture2D* m_color_texture{};
		ID3D11RenderTargetView* m_rtv{};
		ID3D11ShaderResourceView* m_srv{};
		ID3D11Texture2D* m_depth_texture{};
		ID3D11DepthStencilView* m_dsv{};
		std::uint32_t m_width{};
		std::uint32_t m_height{};

		ID3D11Texture2D* m_msaa_color{};
		ID3D11RenderTargetView* m_msaa_rtv{};
		ID3D11Texture2D* m_msaa_depth{};
		ID3D11DepthStencilView* m_msaa_dsv{};
		UINT m_msaa_samples{};

		ID3D11VertexShader* m_vertex_shader{};
		ID3D11PixelShader* m_pixel_shader{};
		ID3D11InputLayout* m_input_layout{};
		ID3D11Buffer* m_cb_scene{};
		ID3D11Buffer* m_cb_bones{};
		ID3D11SamplerState* m_sampler{};
		ID3D11RasterizerState* m_rasterizer{};
		ID3D11DepthStencilState* m_depth_state{};
		ID3D11BlendState* m_blend_state{};

		std::unordered_map<std::string, gpu_texture> m_textures{};
		std::unordered_map<std::string, gpu_material> m_materials{};
		std::unordered_map<std::string, gpu_mesh> m_meshes{};

		float m_yaw{ 0.0f };

		const skinned_mesh* m_mesh{};
		float m_view_projection[ 4 ][ 4 ]{};
		foundation::vec3 m_eye{};
		bool m_ready{};
	};

	inline preview g_preview{};

}
