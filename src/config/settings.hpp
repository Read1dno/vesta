#pragma once

#include <config/combat.hpp>
#include <config/visuals.hpp>
#include <config/misc.hpp>
#include <config/storage.hpp>
#include <memory>

namespace config {

inline combat_profile combat_settings{};
inline visual_profile visual_settings{};
inline general_profile general_settings{};
inline configuration_store storage{};

struct runtime_snapshot
{
	combat_profile combat{};
	visual_profile visual{};
	general_profile general{};
};

inline std::shared_ptr<const runtime_snapshot> g_runtime_snapshot{
	std::make_shared<const runtime_snapshot>( runtime_snapshot{
		combat_settings, visual_settings, general_settings } ) };

inline void publish_runtime_snapshot( )
{
	const auto next = std::make_shared<const runtime_snapshot>( runtime_snapshot{
		combat_settings, visual_settings, general_settings } );
	std::atomic_store_explicit( &g_runtime_snapshot, next, std::memory_order_release );
}

[[nodiscard]] inline std::shared_ptr<const runtime_snapshot> get_runtime_snapshot( )
{
	return std::atomic_load_explicit( &g_runtime_snapshot, std::memory_order_acquire );
}

nlohmann::json build_config_json( );
void apply_config_json( const nlohmann::json& document );
bool apply_default_config( );

} // namespace config
