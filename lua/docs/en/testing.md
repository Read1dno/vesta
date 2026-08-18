# Testing the Lua runtime

The files under `examples/` cover the runtime's manual integration checks:

- `smoke.lua` — API version, controls, events, config schema, storage and drawing;
- `invalid-hot-reload.lua` — an invalid candidate does not replace the active VM;
- `over-budget.lua` — an endless callback is interrupted by the 8 ms budget hook;
- `texture-smoke.lua` — the local WIC/D3D11 texture pipeline.

Suggested sequence:

1. Start `smoke.lua` with autoload and confirm that `ticks` increases.
2. Replace the source with `invalid-hot-reload.lua`; the process remains
   responsive and the active state's `ticks` continues to increase.
3. Restore valid source; `loads` increases exactly once.
4. Start `over-budget.lua`; the process remains responsive, does not enter a
   CPU busy loop, and the state is shown as `Over budget`.
5. Place `ct.png` beside the texture fixture; storage should contain
   `texture_loaded=true`.
