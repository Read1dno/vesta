# Events

Подписка выполняется через `vesta.events.on(name, callback)`. Вызов возвращает
token. `vesta.events.off(name, token)` удаляет одну подписку, а `off(name)` — все.

Доступны:

- `load`, `tick`, `frame`, `unload`;
- `map_changed`;
- `game_connected`, `game_disconnected`;
- `player_joined`, `player_left`, `weapon_changed`;
- `player_hurt`, `player_killed`;
- `projectile_created`, `projectile_detonated`, `projectile_removed`;
- `bomb_planted`, `bomb_defuse_started`, `bomb_defuse_stopped`, `bomb_defused`,
  `bomb_resolved`;
- `confirmed_hit`;
- `config_changed`.

`player_hurt/player_killed` формируются по разнице двух последовательных world
snapshots. `confirmed_hit` использует тот же подтверждённый источник, что
hitmarker/hitsound Vesta. События не запускают отдельный entity scan.

`player_hurt/player_killed` — удобные snapshot-события, а не утверждение о том,
кто нанёс урон. Для подтверждённого локального попадания используйте
`confirmed_hit`.

```lua
vesta.events.on("confirmed_hit", function(event)
    if event.killed then
        vesta.log("Kill, damage: " .. event.damage)
    end
end)
```
