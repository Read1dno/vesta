#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace game {

class variable_registry
{
public:
	[[nodiscard]] std::uintptr_t find( std::uint32_t name_id );

	template<typename T>
	[[nodiscard]] T get( std::uintptr_t cvar_ptr );
};

class field_catalog
{
public:
	[[nodiscard]] bool initialize( );
	[[nodiscard]] std::int32_t lookup( const char* class_name, std::uint32_t field_id );
	[[nodiscard]] std::size_t field_count( ) const noexcept
	{
		return this->m_entries.size( );
	}

private:
	struct field_entry
	{
		std::uint64_t key{};
		std::int32_t offset{};

		[[nodiscard]] bool operator<( const field_entry& other ) const noexcept
		{
			return key < other.key;
		}
	};

	std::vector<field_entry> m_entries{};
};

class entity_directory
{
public:
	enum class type : std::uint8_t
	{
		unknown,
		player,
		item,
		projectile,
		impact
	};

	struct cached
	{
		std::uintptr_t ptr{};
		std::uint32_t schema_id{};
		std::int32_t index{};
		type type{ type::unknown };
	};

	void refresh( );

	[[nodiscard]] std::uintptr_t lookup( std::uint32_t handle ) const;

	[[nodiscard]] std::uintptr_t lookup_index( std::uint32_t index ) const;
	[[nodiscard]] std::vector<cached> by_type( type filter ) const;
	[[nodiscard]] std::shared_ptr<const std::vector<cached>> all( ) const;
	[[nodiscard]] std::uintptr_t raw_entity_list_for_diag( ) const { return get_entity_list( ); }

private:
	struct class_cache_entry
	{
		std::uintptr_t entity_identity{};
		std::uintptr_t entity_class_info{};
		std::uint32_t schema_id{};
		type entity_type{ type::unknown };
	};

	[[nodiscard]] std::uintptr_t get_entity_list( ) const;
	[[nodiscard]] std::uintptr_t lookup_slot( std::uint32_t value,
		bool validate_serial ) const;
	[[nodiscard]] std::uint32_t get_schema_id( std::uintptr_t entity,
		std::uintptr_t& identity, std::uintptr_t& class_info ) const;
	[[nodiscard]] type classify( std::uint32_t schema_id ) const;

	std::atomic<std::shared_ptr<const std::vector<cached>>> m_entities{
		std::make_shared<const std::vector<cached>>( ) };
	std::unordered_map<std::uintptr_t, class_cache_entry> m_class_cache{};
};

}
