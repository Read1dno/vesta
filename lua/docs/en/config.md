# Config API

Lua operates on the same serialized tree as a `.cfg` file:
`combat_global`, `combat_overrides`, `esp`, and `misc`.

```lua
local enabled = vesta.config.get("esp.m_player.enabled")
vesta.config.set("esp.m_player.enabled", true)

vesta.config.patch({
    ["esp.m_player.enabled"] = true,
    ["misc.m_event_log.enabled"] = true
})
```

`get(path)` returns the value or `nil`. `set` and `patch` enqueue a transaction
that is applied to the current configuration on the render/config thread. New
paths and incompatible types are rejected. `snapshot()` returns a copy of the
complete tree. `schema()` returns the same structure without values, using the
types `object`, `array`, `boolean`, `integer`, `number`, and `string`.

Address array entries with a numeric dotted segment, for example
`combat_overrides.0.aimbot.enabled`.
