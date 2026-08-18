#pragma once

#include <config/combat.hpp>
#include <config/visuals.hpp>
#include <config/misc.hpp>
#include <config/storage.hpp>

namespace config {

inline combat_profile combat_settings{};
inline visual_profile visual_settings{};
inline general_profile general_settings{};
inline configuration_store storage{};

nlohmann::json build_config_json( );
void apply_config_json( const nlohmann::json& document );
bool apply_default_config( );

}
