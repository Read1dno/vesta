#include <stdafx.hpp>
#include <features/visuals/visuals.hpp>

namespace features::visuals {

	namespace {
		using subtype = game::world_item_kind;
		enum class family : std::uint8_t
		{
			rifle, smg, shotgun, sniper, pistol, heavy, grenade, utility
		};

		struct descriptor
		{
			subtype type;
			family group;
			std::string_view icon;
			std::string_view label;
		};

		constexpr auto catalog = std::to_array<descriptor>( {
			{ subtype::ak47, family::rifle, "W", "ak47" }, { subtype::m4a4, family::rifle, "S", "m4a4" },
			{ subtype::m4a1s, family::rifle, "T", "m4a1-s" }, { subtype::aug, family::rifle, "U", "aug" },
			{ subtype::famas, family::rifle, "R", "famas" }, { subtype::galil_ar, family::rifle, "Q", "galil" },
			{ subtype::sg553, family::rifle, "V", "sg553" }, { subtype::mac10, family::smg, "K", "mac10" },
			{ subtype::mp5sd, family::smg, "N", "mp5" }, { subtype::mp7, family::smg, "N", "mp7" },
			{ subtype::mp9, family::smg, "R", "mp9" }, { subtype::pp_bizon, family::smg, "M", "bizon" },
			{ subtype::p90, family::smg, "O", "p90" }, { subtype::ump45, family::smg, "L", "ump" },
			{ subtype::nova, family::shotgun, "e", "nova" }, { subtype::sawed_off, family::shotgun, "c", "sawed" },
			{ subtype::xm1014, family::shotgun, "b", "xm1014" }, { subtype::mag7, family::shotgun, "d", "mag7" },
			{ subtype::awp, family::sniper, "Z", "awp" }, { subtype::ssg08, family::sniper, "a", "scout" },
			{ subtype::g3sg1, family::sniper, "X", "g3sg1" }, { subtype::scar20, family::sniper, "Y", "scar" },
			{ subtype::deagle, family::pistol, "A", "deagle" }, { subtype::dual_berettas, family::pistol, "B", "dualies" },
			{ subtype::five_seven, family::pistol, "C", "five7" }, { subtype::glock, family::pistol, "D", "glock" },
			{ subtype::p2000, family::pistol, "E", "p2000" }, { subtype::usps, family::pistol, "G", "usp-s" },
			{ subtype::p250, family::pistol, "F", "p250" }, { subtype::cz75, family::pistol, "I", "cz75" },
			{ subtype::tec9, family::pistol, "H", "tec9" }, { subtype::r8_revolver, family::pistol, "J", "r8" },
			{ subtype::m249, family::heavy, "g", "m249" }, { subtype::negev, family::heavy, "f", "negev" },
			{ subtype::he_grenade, family::grenade, "j", "he" }, { subtype::flashbang, family::grenade, "i", "flash" },
			{ subtype::smoke_grenade, family::grenade, "k", "smoke" }, { subtype::molotov, family::grenade, "l", "molotov" },
			{ subtype::incendiary, family::grenade, "n", "incendiary" }, { subtype::decoy, family::grenade, "m", "decoy" },
			{ subtype::taser, family::utility, "h", "taser" }, { subtype::c4, family::utility, "o", "c4" },
			{ subtype::knife, family::utility, "]", "knife" }, { subtype::healthshot, family::utility, "?", "medkit" },
		} );

		[[nodiscard]] const descriptor* describe( subtype type ) noexcept
		{
			const auto match = std::find_if( catalog.begin( ), catalog.end( ),
				[ type ]( const descriptor& value ) { return value.type == type; } );
			return match == catalog.end( ) ? nullptr : &*match;
		}

		[[nodiscard]] bool enabled( family group,
			const config::visual_profile::item::filters& filter ) noexcept
		{
			switch ( group )
			{
			case family::rifle: return filter.rifles;
			case family::smg: return filter.smgs;
			case family::shotgun: return filter.shotguns;
			case family::sniper: return filter.snipers;
			case family::pistol: return filter.pistols;
			case family::heavy: return filter.heavy;
			case family::grenade: return filter.grenades;
			default: return filter.utility;
			}
		}

		float draw_centered( zdraw::draw_list& target, const foundation::vec2& anchor,
			float offset, std::string_view text, zdraw::font* font,
			const zdraw::rgba& color, float vertical_adjustment )
		{
			zdraw::push_font( font );
			const auto [ width, height ] = zdraw::measure_text( text );
			target.add_text( std::floorf( anchor.x - width * 0.5f ),
				std::floorf( anchor.y + offset + vertical_adjustment ), text,
				nullptr, color, zdraw::text_style::outlined );
			zdraw::pop_font( );
			return height;
		}
	}

	void item_t::on_render( zdraw::draw_list& draw_list )
	{
		const auto& config = config::visual_settings.m_item;
		if ( !config.enabled ) return;
		const auto observer = game::camera().origin( );
		const auto snapshot = game::world().items( );
		for ( const auto& item : *snapshot )
		{
			const auto* metadata = describe( item.subtype );
			if ( !metadata || !enabled( metadata->group, config.m_filters ) ) continue;

			if ( item.subtype == subtype::c4
				&& ::config::visual_settings.m_bomb.enabled
				&& ::config::visual_settings.m_bomb.show_active_bomb ) continue;
			if ( observer.distance( item.origin ) * 0.01905f > config.max_distance ) continue;
			const auto anchor = game::camera().project( item.origin );
			if ( !game::camera().projection_valid( anchor ) ) continue;

			float offset{};
			if ( config.m_icon.enabled )
				offset += draw_centered( draw_list, anchor, offset, metadata->icon,
					app::context().overlay.fonts( ).weapons_esp_15, config.m_icon.color, 0.0f ) - 5.5f;
			if ( config.m_name.enabled )
			{
				constexpr zdraw::rgba label_color{ 240, 240, 245, 220 };
				offset += draw_centered( draw_list, anchor, offset, metadata->label,
					app::context().overlay.fonts( ).esp_text_11, label_color, 0.0f ) - 5.5f;
			}
			if ( config.m_ammo.enabled && item.max_ammo > 0 )
			{
				const auto ammo = std::clamp( item.ammo, 0, item.max_ammo );
				const auto color = ammo > 0 ? config.m_ammo.color : config.m_ammo.empty_color;
				(void)draw_centered( draw_list, anchor, offset,
					std::format( "{}/{}", ammo, item.max_ammo ),
					app::context().overlay.fonts( ).esp_text_11, color, 0.0f );
			}
		}
	}

}
