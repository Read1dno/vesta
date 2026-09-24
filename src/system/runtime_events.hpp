#pragma once
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string_view>

namespace platform::windows
{
inline void runtime_event(std::string_view component, std::string_view state) noexcept
{
    try
    {
        static std::mutex mutex;
        const std::lock_guard lock(mutex);
        wchar_t temporary[MAX_PATH]{};
        const auto count = ::GetTempPathW(MAX_PATH, temporary);
        if (!count || count >= MAX_PATH)
            return;
        const auto directory = std::filesystem::path(temporary) / L"vesta";
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error)
            return;
        const auto path = directory / L"runtime_events.log";
        const auto size = std::filesystem::file_size(path, error);
        std::ofstream output(path, !error && size > 1024 * 1024 ? std::ios::trunc : std::ios::app);
        output << "tick=" << ::GetTickCount64() << " pid=" << ::GetCurrentProcessId()
               << " component=" << component << " state=" << state << '\n';
    }
    catch (...)
    {
    }
}
} // namespace platform::windows
