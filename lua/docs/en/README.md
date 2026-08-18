# Vesta Lua API 1.0

The Lua API lets local scripts consume Vesta's published game state, add menu
controls and overlay graphics, update configuration values, and use the input
API. Scripts receive snapshots through the existing runtime and do not need to
know CS2 addresses or create additional memory-sampling loops.

## Installation

On first launch Vesta creates `%TEMP%\vesta\lua`:

```text
lua/
  scripts/     -- individual .lua files or packages
  modules/     -- shared Lua source modules
  data/        -- private JSON storage for scripts
  logs/
```

Open the directory from `Misc -> Lua API -> Script Directory`, place a file in
`scripts`, or choose `Load Lua Script`. Vesta copies the selected file into the
script directory and refreshes the runtime automatically. Scripts are trusted
local code and have access to standard `io` and `os` functionality.

## Minimal example

```lua
vesta.ui.toggle("enabled", "Player counter", true)

vesta.events.on("frame", function(frame)
    if not vesta.ui.get("enabled") then return end
    vesta.draw.text(30, 90, "Players: " .. #frame.players, 255, 255, 255, 255)
end)
```

Continue with [runtime and lifecycle](runtime-and-lifecycle.md),
[game snapshots](game-snapshot.md), [events](events.md), [configuration](config.md),
[UI and drawing](ui-and-drawing.md), [input](input.md),
[storage and modules](storage-and-helpers.md), [runtime testing](testing.md),
[the Web Radar example](examples/web-radar.md), and the complete
[API reference](api-reference.md).
