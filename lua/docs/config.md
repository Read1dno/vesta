# Config API

Lua работает с тем же сериализованным деревом, что `.cfg`:
`combat_global`, `combat_overrides`, `esp`, `misc`.

```lua
local enabled = vesta.config.get("esp.m_player.enabled")
vesta.config.set("esp.m_player.enabled", true)

vesta.config.patch({
    ["esp.m_player.enabled"] = true,
    ["misc.m_event_log.enabled"] = true
})
```

`get(path)` возвращает значение или `nil`. `set` и `patch` ставят транзакцию в
очередь; она применяется к актуальному config на render/config thread. Новый путь
или несовместимый тип отклоняется. `snapshot()` возвращает копию всего дерева.
`schema()` возвращает такое же дерево без значений с типами `object`, `array`,
`boolean`, `integer`, `number` и `string`.

Индексы массивов указываются числом через точку, например
`combat_overrides.0.aimbot.enabled`.
