#pragma once

#include <render/chams/mesh.hpp>
#include <core/assets/vpk.hpp>
#include <config/settings.hpp>
#include <core/state/pose.hpp>

#include <d3d11.h>

#include <cstdint>
#include <chrono>
#include <future>
#include <string>
#include <unordered_map>
#include <vector>

namespace chams {

	class renderer
	{
	public:
		bool initialize( ID3D11Device* device, ID3D11DeviceContext* context );
		void shutdown( );

		void render_frame( ID3D11RenderTargetView* backbuffer_rtv,
			UINT target_width, UINT target_height,
			const std::shared_ptr<const game::player_pose_frame>& frame );
		void begin_2d_bloom_frame( );
		void add_2d_bloom_segment( float x0, float y0, float x1, float y1,
			float thickness, float radius, zdraw::rgba color );
		void add_2d_bloom_triangle( float x0, float y0, float x1, float y1,
			float x2, float y2, float radius, zdraw::rgba color );
		void render_2d_bloom( ID3D11RenderTargetView* backbuffer_rtv,
			UINT target_width, UINT target_height );

		[[nodiscard]] bool ready( ) const { return this->m_ready; }

		struct diagnostics
		{
			bool renderer_ready{};
			bool vpk_ready{};
			std::string init_error{};

			int players_seen{};
			int players_enemy{};
			int players_model_resolved{};
			int players_mesh_valid{};
			int players_drawn{};
			std::string last_model_path{};
		};

		[[nodiscard]] const diagnostics& diag( ) const { return this->m_diag; }

		[[nodiscard]] vpk_archive& vpk( ) { return this->m_vpk; }
		[[nodiscard]] bool vpk_ready( ) const { return this->m_vpk_ready; }
		[[nodiscard]] bool ensure_vpk( );

		void draw_external( const std::string& model_path, const skinned_mesh& mesh,
			const config::visual_profile::chams::material& material,
			const std::vector<bone_matrix>& skin_matrices,
			const float view_projection[ 4 ][ 4 ], const foundation::vec3& eye );

		static constexpr int k_max_bones{ 128 };

	private:
		struct gpu_mesh
		{
			ID3D11Buffer* vertex_buffer{};
			ID3D11Buffer* index_buffer{};
			std::uint32_t vertex_count{};
			std::uint32_t index_count{};
		};

		struct world_bounds
		{
			foundation::vec3 mins{};
			foundation::vec3 maxs{};
		};

		struct world_chunk
		{
			world_bounds bounds{};
			std::uint32_t first_index{};
			std::uint32_t index_count{};
		};

		struct screen_volume
		{
			float min_x{ -1.0f };
			float min_y{ -1.0f };
			float max_x{ 1.0f };
			float max_y{ 1.0f };
			float max_depth{ 1.0f };
		};

		struct world_geometry
		{
			ID3D11Buffer* vertex_buffer{};
			ID3D11Buffer* index_buffer{};
			std::uint32_t index_count{};
			std::uint64_t revision{};
			std::vector<world_chunk> chunks{};
		};

		bool create_shaders( );
		bool create_constant_buffers( );
		bool create_pipeline_states( );
		void release_gpu_meshes( );

		[[nodiscard]] const gpu_mesh* get_or_upload( const std::string& model_path, const skinned_mesh& mesh );

		[[nodiscard]] static std::string resolve_model_path( std::uintptr_t game_scene_node );

		void update_view_projection( const foundation::matrix4& matrix,
			const foundation::vec3& eye );
		void update_bones( const std::vector<bone_matrix>& skin_matrices );
		void update_bones( ID3D11Buffer* buffer, const std::vector<bone_matrix>& skin_matrices );
		void update_material( const config::visual_profile::chams::material& material,
			float shell_expand = 0.0f, float effect_progress = 0.0f,
			float effect_seed = 0.0f );
		void update_material_pair( const config::visual_profile::chams::material& visible,
			const config::visual_profile::chams::material& invisible, int layer,
			float shell_expand = 0.0f );
		[[nodiscard]] bool ensure_frame_bone_buffers( std::size_t count );

		[[nodiscard]] bool ensure_depth_buffer( UINT width, UINT height );
		void release_depth_buffer( );
		[[nodiscard]] bool ensure_world_depth_buffer( UINT width, UINT height );
		void release_world_depth_buffer( );
		[[nodiscard]] bool ensure_msaa_targets( UINT width, UINT height );
		void resolve_msaa( ID3D11RenderTargetView* backbuffer_rtv,
			const std::vector<screen_volume>& volumes );
		void release_msaa_targets( );
		[[nodiscard]] bool ensure_bloom_targets( UINT width, UINT height );
		void release_bloom_targets( );

		[[nodiscard]] bool ensure_world_geometry( );
		void draw_world_depth( ID3D11DepthStencilView* dsv,
			const std::vector<screen_volume>& occlusion_volumes,
			const foundation::matrix4& view_projection );
		void release_world_geometry( );

		ID3D11Device* m_device{};
		ID3D11DeviceContext* m_context{};

		vpk_archive m_vpk{};
		std::future<vpk_archive> m_vpk_future{};
		bool m_vpk_load_started{};
		bool m_vpk_ready{};

		ID3D11VertexShader* m_vertex_shader{};
		ID3D11PixelShader* m_pixel_shader{};
		ID3D11PixelShader* m_bloom_mask_shader{};
		ID3D11GeometryShader* m_death_geometry_shader{};
		ID3D11InputLayout* m_input_layout{};
		ID3D11VertexShader* m_resolve_vertex_shader{};
		ID3D11PixelShader* m_resolve_pixel_shader{};
		ID3D11VertexShader* m_bloom_vertex_shader{};
		ID3D11PixelShader* m_bloom_blur_shader{};
		ID3D11PixelShader* m_bloom_composite_shader{};
		ID3D11VertexShader* m_bloom_2d_vertex_shader{};
		ID3D11PixelShader* m_bloom_2d_pixel_shader{};
		ID3D11InputLayout* m_bloom_2d_input_layout{};

		ID3D11Buffer* m_cb_view_projection{};
		ID3D11Buffer* m_cb_bones{};
		ID3D11Buffer* m_cb_material{};
		ID3D11Buffer* m_cb_bloom{};
		std::vector<ID3D11Buffer*> m_frame_bone_buffers{};

		ID3D11RasterizerState* m_rs_solid{};
		ID3D11RasterizerState* m_rs_wireframe{};
		ID3D11RasterizerState* m_rs_world_scissor{};
		ID3D11BlendState* m_blend_state{};
		ID3D11BlendState* m_bloom_blend_state{};
		ID3D11BlendState* m_blend_disabled{};
		ID3D11SamplerState* m_bloom_sampler{};
		ID3D11DepthStencilState* m_depth_state{};
		ID3D11DepthStencilState* m_depth_state_read_only{};
		ID3D11DepthStencilState* m_depth_state_disabled{};
		ID3D11DepthStencilState* m_world_depth_state{};

		ID3D11Texture2D* m_depth_texture{};
		ID3D11DepthStencilView* m_dsv{};
		UINT m_depth_width{};
		UINT m_depth_height{};

		ID3D11Texture2D* m_world_depth_texture{};
		ID3D11DepthStencilView* m_world_dsv{};
		ID3D11ShaderResourceView* m_world_depth_srv{};
		UINT m_world_depth_width{};
		UINT m_world_depth_height{};

		ID3D11Texture2D* m_msaa_color{};
		ID3D11RenderTargetView* m_msaa_rtv{};
		ID3D11ShaderResourceView* m_msaa_srv{};
		ID3D11Texture2D* m_msaa_depth{};
		ID3D11DepthStencilView* m_msaa_dsv{};
		UINT m_msaa_width{};
		UINT m_msaa_height{};
		UINT m_msaa_samples{};

		ID3D11Texture2D* m_bloom_a{};
		ID3D11RenderTargetView* m_bloom_a_rtv{};
		ID3D11ShaderResourceView* m_bloom_a_srv{};
		ID3D11Texture2D* m_bloom_b{};
		ID3D11RenderTargetView* m_bloom_b_rtv{};
		ID3D11ShaderResourceView* m_bloom_b_srv{};

		ID3D11Texture2D* m_bloom_source{};
		ID3D11RenderTargetView* m_bloom_source_rtv{};
		ID3D11ShaderResourceView* m_bloom_source_srv{};
		ID3D11Texture2D* m_bloom_inner{};
		ID3D11RenderTargetView* m_bloom_inner_rtv{};
		ID3D11ShaderResourceView* m_bloom_inner_srv{};
		ID3D11Texture2D* m_bloom_depth{};
		ID3D11DepthStencilView* m_bloom_dsv{};
		UINT m_bloom_width{};
		UINT m_bloom_height{};
		struct bloom_2d_vertex
		{
			float position[ 2 ]{};
			float color[ 4 ]{};
		};
		std::vector<bloom_2d_vertex> m_bloom_2d_vertices{};
		ID3D11Buffer* m_bloom_2d_vertex_buffer{};
		std::size_t m_bloom_2d_vertex_capacity{};
		float m_bloom_2d_radius{};

		ID3D11VertexShader* m_world_vertex_shader{};
		ID3D11InputLayout* m_world_input_layout{};

		ID3D11DepthStencilState* m_depth_state_equal{};
		world_geometry m_static_world{};
		world_geometry m_dynamic_world{};

		std::unordered_map<std::string, gpu_mesh> m_gpu_meshes{};
		struct shot_record
		{
			std::string model_path{};
			std::vector<bone_matrix> bones{};
			std::chrono::steady_clock::time_point spawn{};
		};
		std::uint64_t m_last_hit_sequence{};
		std::uint64_t m_last_kill_sequence{};
		std::vector<shot_record> m_shot_records{};
		struct death_pose
		{
			std::string model_path{};
			std::vector<bone_matrix> bones{};
			std::chrono::steady_clock::time_point seen{};
		};
		struct death_record : death_pose
		{
			std::chrono::steady_clock::time_point spawn{};
			float seed{};
		};
		std::unordered_map<std::uintptr_t, death_pose> m_last_death_poses{};
		std::vector<death_record> m_death_records{};

		bool m_ready{};
		diagnostics m_diag{};
	};

	inline renderer g_renderer{};

}
