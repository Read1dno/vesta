#pragma once

#include <cstdlib>
#include <iostream>
#include <utility>

template <typename T>
inline void vesta_check(T&& condition, const char* expression, const char* file, const int line)
{
    if (static_cast<bool>(std::forward<T>(condition)))
        return;

    std::cerr << file << ":" << line << ": check failed: " << expression << "\n";
    std::exit(EXIT_FAILURE);
}

#define VESTA_CHECK(expression) vesta_check((expression), #expression, __FILE__, __LINE__)
