# Vesta Lua API 1.0

Lua API позволяет локальным скриптам читать уже опубликованное состояние Vesta,
добавлять настройки и overlay-графику, менять конфигурацию и использовать внешний
input gateway. Скрипты не получают адреса CS2 и не создают дополнительные циклы
чтения памяти.

## Установка

При первом запуске создаётся `%TEMP%\vesta\lua`:

```text
lua/
  scripts/     -- одиночные .lua или пакеты
  modules/     -- общие Lua-модули, только исходники .lua
  data/        -- private JSON storage скриптов
  logs/
```

Путь можно открыть из `Misc -> Lua API -> Script Directory`. Положите файл в
`scripts` либо нажмите `Load Lua Script` и выберите файл — Vesta атомарно
скопирует его и сразу обновит runtime. Кнопка `Script Directory` открывает эту
папку. Скрипты являются доверенным
локальным кодом: доступны `io`, `os` и запуск внешних процессов.

## Минимальный пример

```lua
vesta.ui.toggle("enabled", "Player counter", true)

vesta.events.on("frame", function(frame)
    if not vesta.ui.get("enabled") then return end
    vesta.draw.text(30, 90, "Players: " .. #frame.players, 255, 255, 255, 255)
end)
```

См. [lifecycle](runtime-and-lifecycle.md), [game snapshot](game-snapshot.md),
[events](events.md), [config](config.md), [UI/draw](ui-and-drawing.md),
[input](input.md), [storage/modules](storage-and-helpers.md),
[проверка runtime](testing.md), [пример Web Radar](examples/web-radar.md) и
[полный reference](api-reference.md).
