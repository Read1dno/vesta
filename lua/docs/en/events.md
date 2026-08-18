# Events

Subscribe with `vesta.events.on(name, callback)`. The call returns a token.
`vesta.events.off(name, token)` removes one subscription; `off(name)` removes
all subscriptions for that event.

Available events:

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

`player_hurt` and `player_killed` are derived from the difference between two
consecutive world snapshots. `confirmed_hit` uses the same confirmed source as
Vesta's hitmarker and hitsound. Events do not start a separate entity scan.

The snapshot events are useful for observing game-state changes, but they do
not assert who caused the damage. Use `confirmed_hit` for a confirmed local hit.

```lua
vesta.events.on("confirmed_hit", function(event)
    if event.killed then
        vesta.log("Kill, damage: " .. event.damage)
    end
end)
```
