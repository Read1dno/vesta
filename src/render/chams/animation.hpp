#pragma once

#include <render/chams/mesh.hpp>
#include <core/assets/vpk.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace chams {

	struct nm_skeleton
	{
		std::vector<std::string> bone_names{};
		std::vector<int> parent_indices{};

		std::vector<foundation::vec3> rest_positions{};

		[[nodiscard]] bool valid( ) const { return !bone_names.empty( ); }
	};

	struct nm_clip
	{
		struct track
		{
			float translation_start[ 3 ]{};
			float translation_length[ 3 ]{};
			float scale_start{ 1.0f };
			float scale_length{};
			foundation::rotation constant_rotation{};
			std::uint32_t read_offset{};
			bool rotation_static{ true };
			bool translation_static{ true };
			bool scale_static{ true };
		};

		std::vector<track> tracks{};
		std::vector<std::uint32_t> frame_offsets{};
		std::vector<std::uint16_t> words{};
		std::uint32_t frame_count{};
		float duration{};
		bool additive{ false };

		[[nodiscard]] bool valid( ) const { return frame_count > 0 && !tracks.empty( ) && !frame_offsets.empty( ); }
	};

	[[nodiscard]] nm_skeleton load_nm_skeleton( vpk_archive& vpk, const std::string& archive_path );
	[[nodiscard]] nm_clip load_nm_clip( vpk_archive& vpk, const std::string& archive_path );

	[[nodiscard]] std::vector<int> map_tracks_to_model( const nm_skeleton& skeleton, const skinned_mesh& mesh );

	void sample_pose( const nm_clip& clip, const nm_skeleton& skeleton, const skinned_mesh& mesh,
		const std::vector<int>& track_to_bone, float time,
		std::vector<bone_matrix>& out, std::vector<bone_matrix>& world );

}
