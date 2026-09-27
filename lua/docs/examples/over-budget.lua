-- Deliberately exceeds the callback execution budget.
vesta.events.on("tick", function()
    while true do end
end)
