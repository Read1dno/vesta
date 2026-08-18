#pragma once

namespace identity {

	inline constexpr std::uint32_t offset_basis{ 2166136261u };
	inline constexpr std::uint32_t prime{ 16777619u };

	[[nodiscard]] constexpr std::uint32_t of( std::string_view text ) noexcept
	{
		auto value = offset_basis;
		for ( const auto character : text )
		{
			value ^= static_cast<std::uint8_t>( character );
			value *= prime;
		}
		return value;
	}

}

[[nodiscard]] constexpr std::uint32_t operator""_id(
	const char* text, std::size_t length ) noexcept
{
	return identity::of( std::string_view{ text, length } );
}
