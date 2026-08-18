#pragma once

#include <windows.h>
#include <tlhelp32.h>
#include <dwmapi.h>
#include <windowsx.h>

#include <array>
#include <atomic>
#include <bit>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>
#include <span>
#include <unordered_map>
#include <numbers>
#include <numeric>
#include <algorithm>
#include <format>
#include <exception>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <future>
#include <functional>
#include <unordered_set>
#include <queue>
#include <string>
#include <string_view>
#include <type_traits>

#include <xorstr.hpp>

#include <core/math/interpolation.hpp>
#include <core/math/vector.hpp>
#include <core/memory/addresses.hpp>
#include <system/storage.hpp>
#include <system/frame_limiter.hpp>
#include <core/state/constants.hpp>
#include <core/memory/symbol.hpp>
#include <core/math/random.hpp>

#include <core/memory/process.hpp>
#include <core/memory/modules.hpp>
#include <core/input/input.hpp>
#include <system/diagnostics.hpp>
#include <system/performance.hpp>

#include <render/draw.hpp>
#include <config/settings.hpp>
#include <core/state/services.hpp>
#include <app/context.hpp>
