# Game snapshot

`vesta.game.snapshot()` возвращает последний immutable frame или `nil` до первой
публикации. Тот же объект передаётся событию `frame`.

Основные поля: `sequence`, `timestamp_us`, `connected`, `map`, `screen`, `camera`,
`local_player`, `players`, `items`, `projectiles`, `spectators`, `bomb`.
`local_player.name` содержит фактический sanitized nickname локального игрока.

`vesta.game.radar_snapshot()` возвращает облегчённый вариант того же immutable
frame для фоновых экспортёров. В нём нет `screen`, spectators, bones, hitboxes,
model path и других тяжёлых ESP-данных; остаются camera/local player, базовые
player/economy/loadout данные, items, projectiles и bomb. В отличие от подписки
на `frame`, функция не создаёт полную Lua-таблицу 60 раз в секунду: скрипт сам
выбирает частоту опроса из `tick`. Web Radar использует 20 Гц.

Запрос данных ленивый. Lua runtime включает сбор players/items/projectiles/bomb
только после обращения к соответствующему snapshot API или подписки на игровое
событие. Поэтому включённый UI-only скрипт не активирует полный world sampler.

## Player

Игрок содержит `handle`, `controller_index`, `steam_id64`, `name`, `health`,
`armor`, `team`, `money`, `ping`, `simulation_tick/time`, `origin`, `velocity`,
`eye_angles`, `weapon`, `loadout` (полный список оружия и гранат),
visibility/radar/state flags, `bones[1..128]` и `hitboxes`.
Адреса pawn/controller/VData намеренно отсутствуют.

Projectile содержит текущие `origin`, `velocity`, неизменяемые стартовые
`initial_position`, `initial_velocity`, а также `kind`, `spawn_time`, `detonate_time`,
`remaining_lifetime`, `expire_time`, `effect_tick_begin`, флаги эффекта и
`fire_points` для фактической границы inferno.

`bomb.position` — мировая позиция установленной бомбы. `bomb.active` и
`bomb.active_position` описывают переносимую либо выпавшую `C_C4`, включая
случай, когда бомба находится у локального игрока.

```lua
for _, player in ipairs(vesta.game.snapshot().players) do
    vesta.log(string.format("%s: %d HP", player.name, player.health))
end
```

## Геометрические запросы

```lua
local screen = vesta.game.world_to_screen({x=0, y=0, z=0})
local hit = vesta.game.trace_ray(start_pos, end_pos)
local damage = vesta.game.bomb_damage(position, site, eye_angles, ducked)
```

`trace_ray` работает на уже построенном collision world и не читает память CS2.
`bomb_damage` использует запечённую карту урона проекта.

```lua
local path = vesta.game.predict_grenade(origin, velocity, "he")
local pen = vesta.game.penetration_damage(eye, point, target.handle)
```

`predict_grenade` принимает `he`, `flash`, `smoke`, `molotov`, `incendiary` или
`decoy` и возвращает точки, отскоки, финальную позицию и время. Расчёт использует
тот же collision world, что штатный grenade prediction.

`penetration_damage` создаёт из опубликованного weapon context независимый
калькулятор и не изменяет общий ballistics state. С `target_handle` возвращаются
урон, hitbox, дистанция и признак пенетрации; без цели функция оценивает первый
прострел в направлении `finish - start`.

`radar_overview(map_name, output_stem)` один раз извлекает штатный radar overview
из локального VPK CS2, декодирует его в PNG внутри приватной директории
данных активного скрипта и возвращает `pos_x`, `pos_y`, `scale`, размеры и данные
нижнего слоя карты. В бинарник Vesta изображения карт не встраиваются.
