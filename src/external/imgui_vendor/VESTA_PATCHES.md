# Vesta ImGui snapshot

Source: https://github.com/ocornut/imgui, with local Vesta patches.
Local source commit: `1c72400873fbccf9a128b8c3abb7597d144f0ea3`.
The upstream remote does not contain this local commit. Required build sources are
vendored here so a fresh Vesta checkout includes cached Win32 frame input and the
single-pass ESP font outline implementation. Files were copied byte-for-byte;
see `SOURCE_HASHES.json`. Original license: `LICENSE.txt`.

The former local repository at `src/external/imgui` is preserved on the developer
machine, ignored by the outer repository, and no longer used by CMake. Updates to
this snapshot must preserve the Vesta-specific APIs and update the hash manifest.
