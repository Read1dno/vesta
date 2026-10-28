#include <stdafx.hpp>
#include <foundation/identity.hpp>
#include <game/state/pose.hpp>
#include <foundation/color.hpp>
#include <game/memory/catalogs.hpp>
#include <game/memory/remote_pointer.hpp>
#include <game/memory/compatibility_contracts.hpp>

namespace game
{

namespace
{
// ConVarData: the current value; +8 holds the default, not the live value.
constexpr std::uintptr_t k_value_offset = 0x58;
// The list-head slot inside CCvar drifts between builds; each candidate is probed and the one that yields a large
// set of named entries wins (a wrong slot yields almost none, the real list has thousands).
constexpr std::array<std::uintptr_t, 6> k_list_head_offsets{0x48, 0x40, 0x50, 0x58, 0x60, 0x68};
constexpr std::size_t k_min_variables = 200;
constexpr std::size_t k_node_stride = 16;
constexpr std::size_t k_nodes_per_chunk = 4096;
constexpr std::size_t k_max_chunks = 8;
constexpr std::size_t k_name_capacity = 128;

[[nodiscard]] bool is_valid_cvar_name(const std::string_view name) noexcept
{
    return name.size() >= 2 && name.size() < k_name_capacity - 1 &&
           std::ranges::all_of(
               name,
               [](const char c)
               {
                   return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                          c == '.';
               });
}
} // namespace

std::uintptr_t variable_registry::find(const std::uint32_t name_id)
{
    if (!m_services)
        return 0;

    std::scoped_lock lock(m_mutex);
    if (m_variables.empty())
    {
        const auto now = clock::now();
        if (m_last_attempt && now - *m_last_attempt < k_retry_interval)
            return 0;
        m_last_attempt = now;
        m_variables = read_list();
    }
    const auto it = m_variables.find(name_id);
    return it == m_variables.end() ? 0 : it->second;
}

std::unordered_map<std::uint32_t, std::uintptr_t> variable_registry::read_list() const
{
    const auto& services = *m_services;
    const auto& process = services.process;
    std::unordered_map<std::uint32_t, std::uintptr_t> variables{};

    const auto cvar = process.locate_vtable_object(services.modules.tier, "CCvar");
    if (!cvar)
    {
        services.log.warning("[convars] CCvar instance not found; console variables unavailable.");
        return variables;
    }

    std::vector<std::uint8_t> buffer(k_nodes_per_chunk * k_node_stride);
    for (const auto head_offset : k_list_head_offsets)
    {
        const auto base = process.load<std::uintptr_t>(cvar + head_offset);
        if (!game::is_user_pointer(base))
            continue;

        for (std::size_t chunk = 0; chunk < k_max_chunks; ++chunk)
        {
            if (!process.copy(base + chunk * buffer.size(), buffer.data(), buffer.size()))
                break;

            for (std::size_t node = 0; node < k_nodes_per_chunk; ++node)
            {
                std::uintptr_t convar{};
                std::memcpy(&convar, buffer.data() + node * k_node_stride, sizeof(convar));
                if (!game::is_user_pointer(convar))
                    continue;
                const auto name_address = process.load<std::uintptr_t>(convar);
                if (!game::is_user_pointer(name_address))
                    continue;
                std::array<char, k_name_capacity> name{};
                if (!process.copy(name_address, name.data(), name.size() - 1))
                    continue;
                if (const std::string_view text{name.data()}; is_valid_cvar_name(text))
                    variables.try_emplace(foundation::identity::of(text), convar);
            }
        }

        if (variables.size() >= k_min_variables)
        {
            services.log.info("[convars] list found at CCvar+{:#x} ({} variables)", head_offset, variables.size());
            return variables;
        }
        variables.clear();
    }

    services.log.warning("[convars] console variable list not found; retrying later.");
    return variables;
}

template<typename T>
T variable_registry::get(std::uintptr_t cvar_ptr)
{
    if (!cvar_ptr || !m_services)
    {
        return T{};
    }

    return m_services->process.load<T>(cvar_ptr + k_value_offset);
}

template<typename T>
bool variable_registry::try_get(std::uintptr_t ptr, T& value)
{
    value = {};
    if (!ptr || !m_services)
        return false;
    std::array<std::byte, 0x5c> data{};
    if (!m_services->process.copy(ptr, data.data(), data.size()))
        return false;
    return compatibility::detail::convar_value(data, value);
}

template bool variable_registry::try_get<bool>(std::uintptr_t, bool&);
template bool variable_registry::try_get<int>(std::uintptr_t, int&);
template bool variable_registry::try_get<float>(std::uintptr_t, float&);

template int variable_registry::get<int>(std::uintptr_t);
template float variable_registry::get<float>(std::uintptr_t);
template bool variable_registry::get<bool>(std::uintptr_t);
template std::uint8_t variable_registry::get<std::uint8_t>(std::uintptr_t);

} // namespace game
