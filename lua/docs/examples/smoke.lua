
assert(vesta.api.major == 1)
assert(debug == nil)

vesta.ui.toggle("enabled", "Smoke overlay", true)
vesta.ui.slider("x", "X", 30, 0, 400, 1)
vesta.ui.select("style", "Style", {"Text", "Circle"}, 0)
vesta.ui.input("label", "Label", "Lua smoke")
vesta.ui.color("accent", "Accent", {r=120, g=200, b=255, a=255})
vesta.ui.keybind("key", "Test key", 0)
vesta.ui.separator("test_section", "Runtime test")
vesta.ui.button("reset", "Reset counter")

local unused = vesta.events.on("unused_test_event", function() end)
assert(vesta.events.off("unused_test_event", unused))
assert(vesta.config.schema() ~= nil)

local ticks = vesta.storage.get("ticks") or 0

vesta.events.on("load", function()
    vesta.storage.set("loaded", true)
    vesta.storage.set("loads", (vesta.storage.get("loads") or 0) + 1)
end)

vesta.events.on("tick", function()
    ticks = ticks + 1
    if ticks % 60 == 0 then vesta.storage.set("ticks", ticks) end
    if vesta.ui.consume("reset") then ticks = 0 end
end)

vesta.events.on("frame", function(frame)
    assert(frame.sequence ~= nil)
    if not vesta.ui.get("enabled") then return end
    local x = vesta.ui.get("x")
    if vesta.ui.get("style") == 0 then
        local color = vesta.ui.get("accent")
        vesta.draw.text(x, 120, vesta.ui.get("label") .. ": " .. #frame.players,
            color.r, color.g, color.b, color.a)
    else
        vesta.draw.circle(x, 120, 10, 120, 200, 255, 255, 2)
    end
end)
