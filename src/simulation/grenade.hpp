#pragma once

#include <cstdint>
#include <vector>

#include <core/math/vector.hpp>

namespace simulation {

inline constexpr foundation::vec3 grenade_collision_half_extents{ 2.0f, 2.0f, 2.0f };

struct grenade_path
{
	std::vector<foundation::vec3> points{};
	std::vector<foundation::vec3> bounces{};
	foundation::vec3 end_pos{};
	float duration{};
	int end_tick{ -1 };
	bool valid{};
};

class grenade_trajectory_engine
{
public:
	[[nodiscard]] grenade_path predict( const foundation::vec3& origin,
		const foundation::vec3& velocity, std::uintptr_t weapon_id,
		float remaining_lifetime = -1.0f ) const;

private:
	struct flight_profile
	{
		float gravity{};
		float fuse_seconds{ 1.5f };
		float rest_speed{};
		float floor_normal_z{};
		bool floor_detonates{};
		bool rest_detonates{};
		bool delayed_fuse{};
	};

	struct step_result
	{
		foundation::vec3 position{};
		foundation::vec3 velocity{};
		bool collided{};
		foundation::vec3 collision_normal{};
	};

	[[nodiscard]] static flight_profile profile_for( std::uintptr_t weapon_id );
	[[nodiscard]] static step_result advance( const foundation::vec3& position,
		const foundation::vec3& velocity, float gravity );
	[[nodiscard]] static foundation::vec3 reflected_velocity( const foundation::vec3& incoming,
		const foundation::vec3& normal );
	[[nodiscard]] static bool fuse_complete( const flight_profile& profile,
		const foundation::vec3& velocity, int tick );

	static constexpr int maximum_ticks{ 1024 };
	static constexpr int sample_stride{ 1 };
	static constexpr float gravity_scale{ 0.4f };
	static constexpr float bounce_elasticity{ 0.45f };
	static constexpr float collision_skin{ 1.0f / 32.0f };
	static constexpr int maximum_contacts_per_tick{ 4 };
};

}
