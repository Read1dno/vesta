# Runtime и lifecycle

Каждый активный скрипт получает отдельные `lua_State` и background thread с
приоритетом `BELOW_NORMAL`. Lua никогда не вызывается на render thread. Команды
отрисовки публикуются immutable-буфером и появляются в следующем presentation
frame.

## События lifecycle

```lua
vesta.events.on("load", function() end)
vesta.events.on("tick", function(dt) end)       -- около 60 Гц
vesta.events.on("frame", function(snapshot) end)
vesta.events.on("unload", function() end)
```

`frame` использует latest-only семантику: медленный скрипт не накапливает старые
кадры. Hot Reload проверяет изменение entry-файла и перезапускает только выбранный
скрипт. Все удерживаемые им клавиши и кнопки отпускаются при reload, stop, ошибке,
открытии меню, потере game input и завершении Vesta.

Новый source сначала компилируется и выполняет top-level в provisional VM. До
успешной замены ему запрещены config/input side effects; `load` приходит только
после атомарной замены. При ошибке рабочий VM продолжает исполняться, а сообщение
показывается в меню. `unload` старого state вызывается ровно один раз.

Лимиты одного скрипта: 64 MiB Lua heap, 8192 draw-команд, 512 UI-контролов и
8 ms на один Lua callback. Превышение отображается во вкладке Lua API.

Game frame публикуется не чаще 60 Гц. Config snapshot обновляется с частотой до
10 Гц или немедленно после patch, но только если активный скрипт обращался к
`vesta.config.*` либо подписался на `config_changed`. Players/items/projectiles/
spectators/bomb также собираются лениво по фактически запрошенным API и событиям.
Если активных скриптов нет, Lua bridge полностью бездействует.

## Package manifest

Пакет располагается в отдельной директории:

```json
{
  "id": "example_stats",
  "name": "Example Stats",
  "version": "1.0.0",
  "author": "author",
  "description": "Local statistics",
  "api_version": 1,
  "entry": "main.lua"
}
```

`id` содержит латинские буквы, цифры, `_` и `-`. Autoload и Hot Reload являются
пользовательскими настройками Vesta и не задаются manifest-файлом.
