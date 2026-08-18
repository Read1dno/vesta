# API reference 1.0

## Core

- `vesta.api.major`, `vesta.api.minor`
- `vesta.api.script_dir` — absolute directory of the active package
- `vesta.api.data_dir` — absolute private writable data directory of the active package
- `vesta.log(text)`
- `vesta.events.on(name, callback) -> token`
- `vesta.events.off(name [, token]) -> bool`

## Game

- `vesta.game.snapshot()`
- `vesta.game.radar_snapshot()`
- `vesta.game.world_to_screen(vec3)`
- `vesta.game.trace_ray(start, finish)`
- `vesta.game.bomb_damage(position [, site, eye_angles, ducked])`
- `vesta.game.predict_grenade(origin, velocity, kind [, remaining_lifetime])`
- `vesta.game.radar_overview(map_name [, output_stem])`
- `vesta.game.penetration_damage(start, finish [, target_handle])`

## Config and storage

- `vesta.config.get(path)`
- `vesta.config.set(path, value)`
- `vesta.config.patch(table)`
- `vesta.config.snapshot()`
- `vesta.config.schema()`
- `vesta.storage.get/set/remove(key [, value])`

## UI

- `vesta.ui.text(id, label, value)`
- `vesta.ui.button(id, label [, action_text])`
- `vesta.ui.toggle(id, label, default)`
- `vesta.ui.slider(id, label, default, min, max, step)`
- `vesta.ui.select(id, label, options, default_index)`
- `vesta.ui.input(id, label, default_text)`
- `vesta.ui.color(id, label, {r,g,b,a})`
- `vesta.ui.keybind(id, label, default_vk)`
- `vesta.ui.separator(id, label)`
- `vesta.ui.get(id)`, `vesta.ui.consume(id)`

## External helpers

- `vesta.helpers.start(relative_ps1, args) -> bool [, error]`
- `vesta.helpers.copy_text(text) -> bool`

`start` accepts a `.ps1` file inside the active package and returns immediately
after creating the external process.

## Bundled assets

- `vesta.assets.weapon_font() -> string`

Returns the bytes of the same TTF Vesta uses for built-in weapon and grenade
icons. A script can write the string in binary mode and use the font in a local
UI or web frontend.

## Draw

- `line(x1,y1,x2,y2,r,g,b,a[,thickness])`
- `rect/filled_rect(x,y,w,h,r,g,b,a[,thickness])`
- `circle/filled_circle(x,y,radius,r,g,b,a[,thickness])`
- `text(x,y,text,r,g,b,a)`
- `world_line(vec3,vec3,r,g,b,a[,thickness])`
- `world_text(vec3,text,r,g,b,a)`
- `load_texture(relative_path) -> texture_id | nil,error`
- `image(texture_id,x,y,w,h[,r,g,b,a])`
- `panel(x,y,w,h,title[,accent_r,g,b,a])`

## Input

- `is_down(vk)`, `binding(action)`, `key(vk,down)`, `tap(vk)`
- `mouse_move(dx,dy)`, `mouse_button("primary"|"secondary",down)`
