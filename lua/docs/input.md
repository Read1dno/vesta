# Input API

```lua
vesta.input.is_down(0x20)
local jump = vesta.input.binding("jump")
vesta.input.key(0x20, true)
vesta.input.key(0x20, false)
vesta.input.tap(0x20)
vesta.input.mouse_move(4, -2)
vesta.input.mouse_button("primary", true)
vesta.input.mouse_button("primary", false)
```

Имена binding: `forward`, `back`, `left`, `right`, `walk`, `duck`, `jump`,
`attack`, `attack2`. Новые нажатия блокируются при открытом меню или неготовом
game input. Runtime отслеживает ownership каждого скрипта и гарантированно
отпускает его состояния при остановке.
