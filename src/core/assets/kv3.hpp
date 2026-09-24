#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace chams::kv3 {

	class object; // forward decl, values may nest objects/arrays

	using array = std::vector<object>;
	using dict = std::map<std::string, object>; // insertion order not required for lookup use

	// A KV3 value. NULL_VALUE is distinct from "key absent" -- callers doing
	// dict lookups should check both.
	class object
	{
	public:
		using value_t = std::variant<
			std::monostate, // NULL_VALUE
			bool,
			std::int64_t,
			std::uint64_t,
			double,
			std::string,
			std::vector<std::uint8_t>, // BINARY_BLOB
			array,
			dict
		>;

		object( ) = default;
		object( value_t v ) : m_value( std::move( v ) ) { }

		[[nodiscard]] bool is_dict( ) const { return std::holds_alternative<dict>( m_value ); }
		[[nodiscard]] bool is_array( ) const { return std::holds_alternative<array>( m_value ); }
		[[nodiscard]] bool is_null( ) const { return std::holds_alternative<std::monostate>( m_value ); }

		[[nodiscard]] const object* find( const std::string& key ) const;
		[[nodiscard]] const object* at( std::size_t index ) const;
		[[nodiscard]] std::size_t size( ) const;

		[[nodiscard]] std::string as_string( ) const;
		[[nodiscard]] double as_double( ) const;
		[[nodiscard]] std::int64_t as_int( ) const;
		[[nodiscard]] bool as_bool( ) const;

		[[nodiscard]] bool as_float3( float out[ 3 ] ) const;
		[[nodiscard]] bool as_float4( float out[ 4 ] ) const;

		value_t m_value{};
	};

	struct document
	{
		std::array<std::uint8_t, 16> format_guid{};
		object root{};
	};

	[[nodiscard]] document decode( const std::uint8_t* data, std::size_t size );

	inline document decode( const std::vector<std::uint8_t>& data )
	{
		return decode( data.data( ), data.size( ) );
	}

} // namespace chams::kv3
