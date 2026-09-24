#pragma once

#include <string_view>

namespace render::localization
{

enum class id : int
{
    en = 0,
    ru = 1,
    zh_cn = 2,
    zh_tw = 3,
    count
};

void set(id value);
[[nodiscard]] id current();

// Short label for the language switch itself ("EN" / "RU" / "CN" / "TW").
[[nodiscard]] const char *code(id value);

[[nodiscard]] const char *tr(const char *english);

// Same, for text built at runtime. Only the fixed fragments are looked up.
[[nodiscard]] std::string_view tr(std::string_view english);

} // namespace render::localization
