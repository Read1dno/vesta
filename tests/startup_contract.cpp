#include <app/startup_stage.hpp>
#include <windows.h>
#include <cstdio>
#include <string_view>
#include <string>

int main(int argc, char **argv)
{
	using app::startup_stage;
	if (app::select_startup_stage(false, false) != startup_stage::request_elevation ||
	    app::select_startup_stage(true, false) != startup_stage::prepare_ui_access ||
	    app::select_startup_stage(true, true) != startup_stage::run ||
	    app::select_startup_stage(false, true) != startup_stage::run)
		return 1;
	if (argc != 4)
		return 2;
	const auto exe =
	    ::LoadLibraryExA(argv[1], nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
	if (!exe)
		return 3;
	const auto resource = ::FindResourceW(exe, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(24));
	const auto data = resource ? ::LoadResource(exe, resource) : nullptr;
	if (!data)
	{
		::FreeLibrary(exe);
		return 4;
	}
	const std::string_view manifest(static_cast<const char *>(::LockResource(data)),
	                                ::SizeofResource(exe, resource));
	const auto attribute = [&](std::string_view name, std::string_view expected) {
		const auto prefix = std::string(name) + "=";
		return manifest.find(prefix + "\"" + std::string(expected) + "\"") != std::string_view::npos ||
		       manifest.find(prefix + "'" + std::string(expected) + "'") != std::string_view::npos;
	};
	const auto valid = attribute("level", argv[2]) && attribute("uiAccess", argv[3]);
	if (!valid)
		std::printf("manifest bytes=%zu: %.*s\n", manifest.size(), static_cast<int>(manifest.size()),
		            manifest.data());
	::FreeLibrary(exe);
	if (!valid)
	{
		std::puts("FAIL startup manifest");
		return 5;
	}
	std::printf("PASS embedded manifest level=%s uiAccess=%s; startup stages\n", argv[2], argv[3]);
}
