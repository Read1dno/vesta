local texture, error_message = vesta.draw.load_texture("ct.png")
vesta.storage.set("texture_loaded", texture ~= nil)
vesta.storage.set("texture_error", error_message)

vesta.events.on("frame", function()
    if texture then
        vesta.draw.image(texture, 20, 20, 48, 48, 255, 255, 255, 255)
    end
end)
