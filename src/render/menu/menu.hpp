#pragma once

#include <windows.h>

#include <atomic>

class menu_t
{
public:
	bool initialize( HWND hwnd );
	void poll_hotkey( );
	void draw( );
	void map_pointer_to_layout(
		float& x, float& y, float display_width, float display_height ) const noexcept;
	void map_pointer_to_screen(
		float& x, float& y, float display_width, float display_height ) const noexcept;
	void close( ) noexcept { this->m_open.store( false, std::memory_order_release ); }
	[[nodiscard]] bool is_open( ) const noexcept
	{
		return this->m_open.load( std::memory_order_acquire );
	}

private:
	void draw_sidebar( );
	void draw_content( );
	void draw_visual_editor( );
	void draw_combat( bool triggerbot );
	void draw_visuals( );
	void draw_misc( );
	void reset_content_animation( );

	std::atomic<bool> m_open{};
	bool m_menu_hotkey_was_down{};
	bool m_open_pending{};
	HWND m_hwnd{};
	int m_page{};
	int m_weapon_group{ -1 };
	int m_visual_group{};
	int m_misc_group{};
	float m_content_animation{ 1.0f };
	float m_visual_editor_animation{};
};
