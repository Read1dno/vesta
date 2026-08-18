# Game snapshot

`vesta.game.snapshot()` returns the latest immutable frame, or `nil` before the
first frame is published. The same object is passed to the `frame` event.

Top-level fields are `sequence`, `timestamp_us`, `connected`, `map`, `screen`,
`camera`, `local_player`, `players`, `items`, `projectiles`, `spectators`, and
`bomb`. `local_player.name` contains the sanitized nickname of the local player.

`vesta.game.radar_snapshot()` returns a lightweight version of the same frame
for background exporters. It omits `screen`, spectators, bones, hitboxes, model
paths, and other heavy ESP data while retaining camera/local-player state,
basic player economy and loadout data, items, projectiles, and bomb state. Unlike
a `frame` subscription, the function does not build a complete Lua table 60
times per second; the script chooses its polling rate from `tick`. Web Radar
uses 20 Hz.

Data collection is lazy. The runtime enables player, item, projectile, and bomb
collection only after a corresponding snapshot API is called or a game event is
subscribed. A UI-only script does not activate the complete world sampler.

## Player

A player contains `handle`, `controller_index`, `steam_id64`, `name`, `health`,
`armor`, `team`, `money`, `ping`, `simulation_tick/time`, `origin`, `velocity`,
`eye_angles`, `weapon`, `loadout`, visibility/radar/state flags,
`bones[1..128]`, and `hitboxes`.

A projectile contains its current `origin` and `velocity`, immutable
`initial_position` and `initial_velocity`, plus `kind`, `spawn_time`,
`detonate_time`, `remaining_lifetime`, `expire_time`, `effect_tick_begin`, effect
flags, and `fire_points` for the actual inferno boundary.

`bomb.position` is the planted bomb's world position. `bomb.active` and
`bomb.active_position` describe a carried or dropped `C_C4`, including when the
local player carries it.

```lua
for _, player in ipairs(vesta.game.snapshot().players) do
    vesta.log(string.format("%s: %d HP", player.name, player.health))
end
```

## Geometry queries

```lua
local screen = vesta.game.world_to_screen({x=0, y=0, z=0})
local hit = vesta.game.trace_ray(start_pos, end_pos)
local damage = vesta.game.bomb_damage(position, site, eye_angles, ducked)
```

`trace_ray` uses the already-built collision world. `bomb_damage` uses Vesta's
baked bomb-damage map.

```lua
local path = vesta.game.predict_grenade(origin, velocity, "he")
local pen = vesta.game.penetration_damage(eye, point, target.handle)
```

`predict_grenade` accepts `he`, `flash`, `smoke`, `molotov`, `incendiary`, or
`decoy` and returns path points, bounces, final position, and time. It shares the
same collision world as Vesta's grenade prediction.

`penetration_damage` creates an independent calculator from the published
weapon context. With `target_handle` it returns damage, hitbox, distance, and
penetration state; without a target it evaluates the first penetration along
`finish - start`.

`radar_overview(map_name, output_stem)` extracts the game's radar overview from
the local CS2 VPK once, decodes it to PNG inside the active script's private data
directory, and returns `pos_x`, `pos_y`, `scale`, dimensions, and lower-level map
data. Map images are not embedded in Vesta.
