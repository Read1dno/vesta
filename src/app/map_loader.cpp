#include <stdafx.hpp>
#include <app/map_loader.hpp>
#include <app/context.hpp>

namespace app
{
namespace
{

void log_map(const char *event, const char *map, const char *error = "") noexcept
{
	wchar_t path[MAX_PATH]{};
	const auto length = ::GetTempPathW(MAX_PATH, path);
	if (!length || length >= MAX_PATH - 32)
		return;
	::wcscat_s(path, L"vesta");
	::CreateDirectoryW(path, nullptr);
	::wcscat_s(path, L"\\map_loading.log");
	const auto file =
	    ::CreateFileW(path, FILE_APPEND_DATA | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
	                  OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return;
	LARGE_INTEGER size{};
	if (::GetFileSizeEx(file, &size) && size.QuadPart > 1024 * 1024)
	{
		::SetFilePointer(file, 0, nullptr, FILE_BEGIN);
		::SetEndOfFile(file);
	}
	char line[512]{};
	const auto bytes = ::_snprintf_s(line, _TRUNCATE, "tick=%llu pid=%lu %s map=%.80s %.200s\r\n",
	                                 ::GetTickCount64(), ::GetCurrentProcessId(), event, map, error);
	OVERLAPPED append{};
	append.Offset = append.OffsetHigh = 0xffffffffu;
	DWORD written{};
	if (bytes > 0)
		::WriteFile(file, line, static_cast<DWORD>(bytes), &written, &append);
	::CloseHandle(file);
}

} // namespace

map_loader::map_loader() : worker_([this](std::stop_token stop) { run(stop); })
{
}

map_loader::~map_loader()
{
	worker_.request_stop();
	queue_.cancel();
}

void map_loader::request(std::string map)
{
	log_map("requested", map.c_str());
	queue_.request(std::move(map), [] {
		game::collision().clear();
		game::blast_damage().clear();
	});
}

void map_loader::run(std::stop_token stop)
{
	const auto background = ::SetThreadPriority(::GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
	if (!background)
		::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
	std::stop_callback cancel(stop, [this] { queue_.cancel(); });
	while (const auto job = queue_.next(stop))
	{
		if (stop.stop_requested())
			break;
		if (job->key.empty())
			continue;
		try
		{
			game::collision_world built;
			const auto from_file = built.build_from_map_file(job->key, job->stop);
			if (!from_file && !job->stop.stop_requested())
			{
				log_map("live_fallback", job->key.c_str());
				built.parse(job->stop);
			}
			if (!queue_.commit(job->stop, [&] { game::collision().replace_with(built); }))
				continue;
			log_map(game::collision().valid() ? "geometry_ready" : "geometry_unavailable", job->key.c_str());
			game::blast_model blast;
			blast.parse(job->stop);
			if (!queue_.commit(job->stop, [&] { game::blast_damage().replace_with(blast); }))
				continue;
			if (from_file)
			{
				std::mutex wait_mutex;
				std::condition_variable_any wake;
				std::unique_lock wait_lock(wait_mutex);
				while (!job->stop.stop_requested())
				{
					wake.wait_for(wait_lock, job->stop, std::chrono::milliseconds(100), [] { return false; });
					if (job->stop.stop_requested())
						break;
					game::collision().refresh_map_entities(job->stop);
				}
			}
		}
		catch (const std::exception &error)
		{
			// Unwind the private build before logging; never terminate unrelated workers.
			log_map("load_failed", job->key.c_str(), error.what());
		}
		catch (...)
		{
			log_map("load_failed", job->key.c_str(), "unknown exception");
		}
	}
	if (background)
		::SetThreadPriority(::GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
}

} // namespace app
