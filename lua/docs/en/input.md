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

Binding names are `forward`, `back`, `left`, `right`, `walk`, `duck`, `jump`,
`attack`, and `attack2`. New presses are blocked while the menu is open or game
input is unavailable. The runtime tracks ownership per script and releases all
owned states when that script stops.
