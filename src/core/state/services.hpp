#pragma once

#include <core/memory/catalogs.hpp>
#include <core/state/runtime.hpp>
#include <core/state/pose.hpp>
#include <simulation/target.hpp>
#include <core/state/snapshot.hpp>
#include <simulation/collision.hpp>
#include <simulation/blast.hpp>

namespace game {

inline variable_registry& variables( ) { static variable_registry value{}; return value; }
inline field_catalog& fields( ) { static field_catalog value{}; return value; }
inline entity_directory& entity_index( ) { static entity_directory value{}; return value; }
inline local_state& local_player( ) { static local_state value{}; return value; }
inline camera_state& camera( ) { static camera_state value{}; return value; }
inline player_pose_sampler& render_poses( ) { static player_pose_sampler value{}; return value; }
inline skeleton_reader& skeletons( ) { static skeleton_reader value{}; return value; }
inline bounds_projector& projection_bounds( ) { static bounds_projector value{}; return value; }
inline hitbox_catalog& hitbox_data( ) { static hitbox_catalog value{}; return value; }
inline world_sampler& world( ) { static world_sampler value{}; return value; }
inline collision_world& collision( ) { static collision_world value{}; return value; }
inline blast_model& blast_damage( ) { static blast_model value{}; return value; }

}

#define SCHEMA( class_name, field_id ) \
	[]( ) -> std::int32_t { \
		static const auto value = game::fields( ).lookup( class_name, field_id ); \
		return value; \
	}( )

#define CONVAR( name_id ) \
	[]( ) -> std::uintptr_t { \
		static const auto value = game::variables( ).find( name_id ); \
		return value; \
	}( )
