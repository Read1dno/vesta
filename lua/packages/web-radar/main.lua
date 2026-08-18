

local root = vesta.api.script_dir
local runtime = vesta.api.data_dir
local separator = package.config:sub(1, 1)
local embedded_helper = rawget(_G, "__VESTA_WEB_RADAR_HELPER")
local embedded_html = rawget(_G, "__VESTA_WEB_RADAR_HTML")
local helper_relative = embedded_helper and ".vesta-web-radar-helper.ps1" or "helper.ps1"
local html_path = embedded_html and (runtime .. separator .. "web-radar.html")
    or (root .. separator .. "public" .. separator .. "index.html")
local identity = tostring({}):match("0x(%x+)") or tostring(os.time())
local session = tostring(os.time()) .. "-" .. identity
local prefix = runtime .. separator .. session .. "-"
local state_paths = {prefix .. "state-a.json", prefix .. "state-b.json"}
local state_temporary = prefix .. "state.tmp"
local state_slot = 1
local map_meta_path = prefix .. "map-meta.json"
local map_meta_tmp = prefix .. "map-meta.tmp"
local status_path = prefix .. "status.json"
local stop_path = prefix .. "stop.flag"

local running = false
local status = "Starting..."
local share_url = ""
local next_write_us = 0
local write_accumulator = 0
local status_accumulator = 0
local map_name = ""
local overview = nil
local trajectory_cache = {}
local effect_cache = {}
local copy_feedback_until = 0
local copy_action = "Starting..."

local function write_file(path, value)
    local file = io.open(path, "wb")
    if not file then return false end
    file:write(value)
    file:close()
    return true
end

local function ensure_embedded_assets()
    if not embedded_helper then return true end
    if type(embedded_helper) ~= "string" or type(embedded_html) ~= "string" then return false end
    return write_file(root .. separator .. helper_relative, embedded_helper)
        and write_file(html_path, embedded_html)
end

local function ensure_weapon_font()
	local target = prefix .. "weapon-icons.ttf"
    local existing = io.open(target, "rb")
    if existing then
        local size = existing:seek("end") or 0
        existing:close()
        if size > 1024 then return true end
    end
    local bytes = vesta.assets.weapon_font()
    if type(bytes) ~= "string" or #bytes < 1024 then return false end
    local file = io.open(target, "wb")
    if not file then return false end
    file:write(bytes)
    file:close()
    return true
end

vesta.ui.button("copy", "Copy link", "Starting...")

local function set_copy_action(value)
	value = tostring(value or "")
	if value == copy_action then return end
	copy_action = value
	vesta.ui.button("copy", "Copy link", value)
end

local function json_string(value)
    local escaped = tostring(value or "")
        :gsub('\\', '\\\\'):gsub('"', '\\"')
        :gsub('\b', '\\b'):gsub('\f', '\\f')
        :gsub('\n', '\\n'):gsub('\r', '\\r'):gsub('\t', '\\t')
    return '"' .. escaped .. '"'
end

local function number(value, fallback)
    value = tonumber(value)
    if not value or value ~= value or value == math.huge or value == -math.huge then
        return fallback or 0
    end
    return value
end

local function bool(value) return value and "true" or "false" end
local function normalize_map(value)
    value = tostring(value or ""):gsub('\\', '/')
    return value:match('([^/]+)%.vpk$') or value:match('([^/]+)$') or ""
end
local function vec3(value)
    value = value or {}
    return string.format('[%.2f,%.2f,%.2f]', number(value.x), number(value.y), number(value.z))
end

local function read_file(path)
    local file = io.open(path, "rb")
    if not file then return nil end
    local value = file:read("*a")
    file:close()
    return value
end

local function write_atomic_path(path, temporary, value)
    local file = io.open(temporary, "wb")
    if not file then return false end
    file:write(value)
    file:close()
    os.remove(path)
    return os.rename(temporary, path) ~= nil
end

local function write_state(value)
    local target = state_paths[state_slot]
    if not write_atomic_path(target, state_temporary, value) then return false end
    state_slot = state_slot == 1 and 2 or 1
    return true
end

local function write_stop()
    local file = io.open(stop_path, "wb")
    if file then file:write("stop"); file:close() end
end

local function json_field(source, name)
    if not source then return nil end
    local raw = source:match('"' .. name .. '"%s*:%s*"(.-)"')
    if not raw then return nil end
    return raw:gsub('\\/', '/'):gsub('\\"', '"'):gsub('\\\\', '\\')
end

local function refresh_status()
    local source = read_file(status_path)
    if not source then return end
    local phase = json_field(source, "status")
    local public = json_field(source, "public_url")
    local err = json_field(source, "error")
    running = phase ~= "stopped" and phase ~= "error"
    if public and public ~= "" then
        share_url, status = public, "Ready - private link available"
        if os.clock() >= copy_feedback_until then set_copy_action("Copy link") end
    elseif phase == "downloading-cloudflared" then
        status = "Downloading Cloudflare Tunnel..."
    elseif phase == "verifying-cloudflared" then
        status = "Verifying Cloudflare Tunnel..."
    elseif phase == "reconnecting-tunnel" then
        status = "Reconnecting Cloudflare Tunnel..."
    elseif phase == "local-only" then
        status = err and err ~= "" and ("Tunnel unavailable: " .. err) or "Public tunnel unavailable"
    elseif phase == "error" then
        status = err and err ~= "" and ("Error: " .. err) or "Server error"
        set_copy_action("Unavailable")
    else
        status = "Opening private link..."
        set_copy_action("Starting...")
    end
end

local function prepare_overview(frame)
    map_name = normalize_map(frame.map)
    trajectory_cache = {}
    effect_cache = {}
    overview = nil
    if map_name == "" then return end
    local value, err = vesta.game.radar_overview(map_name, "overview-" .. map_name)
    if not value then
        status = "Map unavailable: " .. tostring(err or map_name)
        set_copy_action("Map unavailable")
        return
    end
    overview = value
    local meta = string.format(
        '{"map":%s,"pos_x":%.3f,"pos_y":%.3f,"scale":%.5f,"width":%d,"height":%d,' ..
        '"has_lower":%s,"lower_altitude_max":%.3f}',
        json_string(map_name), number(value.pos_x), number(value.pos_y), number(value.scale, 1),
        number(value.width, 1024), number(value.height, 1024), bool(value.has_lower),
        number(value.lower_altitude_max))
    write_atomic_path(map_meta_path, map_meta_tmp, meta)
end

local grenade_names = {[1]="he",[2]="flash",[3]="smoke",[4]="molotov",[6]="decoy"}
local function trajectory_json(frame, projectile)
    local kind = grenade_names[number(projectile.kind)]
	local velocity = projectile.initial_velocity or {}
	local speed_sq = number(velocity.x)^2 + number(velocity.y)^2 + number(velocity.z)^2
	if speed_sq <= 64 then
		velocity = projectile.velocity or {}
		speed_sq = number(velocity.x)^2 + number(velocity.y)^2 + number(velocity.z)^2
	end
	local origin = projectile.initial_position or {}
	if number(origin.x)^2 + number(origin.y)^2 + number(origin.z)^2 <= 1 then
		origin = projectile.origin or {}
	end
	local moving = speed_sq > 64
    if not kind or not moving or projectile.detonated or projectile.smoke_active then return "[]", false end
    local id = number(projectile.id)
    local cached = trajectory_cache[id]
	local signature = string.format("%.2f:%.2f:%.2f:%.2f:%.2f:%.2f:%s",
		number(origin.x), number(origin.y), number(origin.z), number(velocity.x),
		number(velocity.y), number(velocity.z), kind)
	if not cached or cached.signature ~= signature then
		local predicted = vesta.game.predict_grenade(origin, velocity, kind, -1)
        local points = {}
        if predicted and predicted.valid then
            local source = predicted.points or {}
            local stride = math.max(1, math.ceil(#source / 48))
            for index = 1, #source, stride do points[#points+1] = vec3(source[index]) end
            if #source > 0 and ((#source - 1) % stride) ~= 0 then points[#points+1] = vec3(source[#source]) end
        end
		cached = {signature=signature, json="[" .. table.concat(points, ",") .. "]"}
        trajectory_cache[id] = cached
		return cached.json, true
    end
    return cached.json, false
end

local function prime_one_trajectory(frame)
    for _, projectile in ipairs(frame.projectiles or {}) do
        local _, generated = trajectory_json(frame, projectile)
        if generated then return true end
    end
    return false
end

local function bomb_position(frame, bomb)
    if bomb.planted and bomb.position then return bomb.position end
	if bomb.active and bomb.active_position then return bomb.active_position end
    for _, item in ipairs(frame.items or {}) do
        if number(item.kind) == 37 then return item.origin end
    end
    for _, player in ipairs(frame.players or {}) do
        for _, name in ipairs(player.loadout or {}) do
            name = tostring(name):lower():gsub("^weapon_", "")
            if name == "c4" then return player.origin end
        end
    end
    return nil
end

local function string_array(values)
    local output = {}
    for _, value in ipairs(values or {}) do output[#output+1] = json_string(value) end
    return "[" .. table.concat(output, ",") .. "]"
end

local function encode_players(frame)
    local output = {}
    for _, p in ipairs(frame.players or {}) do
        local weapon = p.weapon or {}
        output[#output+1] = string.format(
            '{"id":%d,"name":%s,"team":%d,"hp":%d,"armor":%d,"origin":%s,"velocity":%s,"angles":%s,' ..
            '"weapon":%s,"ammo":%d,"max_ammo":%d,"money":%d,"helmet":%s,"kit":%s,"scoped":%s,' ..
            '"loadout":%s,"visible":%s,"spotted":%s,"immune":%s}',
            number(p.handle), json_string(p.name), number(p.team), number(p.health), number(p.armor),
            vec3(p.origin), vec3(p.velocity), vec3(p.eye_angles), json_string(weapon.name), number(weapon.ammo),
            number(weapon.max_ammo), number(p.money), bool(p.helmet), bool(p.defuser), bool(p.scoped),
            string_array(p.loadout), bool(p.visible), bool(p.spotted), bool(p.invulnerable))
    end
    return '[' .. table.concat(output, ',') .. ']'
end

local function encode_projectiles(frame)
    local output, alive = {}, {}
    for _, p in ipairs(frame.projectiles or {}) do
        local id = number(p.id)
        alive[id] = true
        local kind = number(p.kind)
        local active_effect = p.smoke_active or kind == 5
            or (kind == 6 and number(p.effect_tick_begin) > 0)
        local duration = kind == 3 and 20 or kind == 5 and 7 or kind == 6 and 15 or 0
        local effect = effect_cache[id]
        if active_effect and not effect then
            effect = {started=number(frame.timestamp_us)}
            effect_cache[id] = effect
        end
        local precise_expire = number(p.expire_time)
        local game_time = number(frame.local_player and frame.local_player.game_time)
        local effect_remaining = precise_expire > game_time and game_time > 0
            and (precise_expire - game_time)
            or (effect and math.max(0,
                duration - (number(frame.timestamp_us) - effect.started) / 1000000) or -1)

        if not ((kind == 1 or kind == 2) and p.detonated)
            and not (active_effect and duration > 0 and effect_remaining <= 0) then
            local fires = {}
            for _, point in ipairs(p.fire_points or {}) do fires[#fires+1] = vec3(point) end
            local trajectory = trajectory_json(frame, p)
            output[#output+1] = string.format(
                '{"id":%d,"kind":%d,"origin":%s,"velocity":%s,"remaining":%.2f,' ..
                '"effect_remaining":%.2f,"detonated":%s,"smoke":%s,"bounces":%d,' ..
                '"spawn":%.3f,"detonate":%.3f,"expire":%.3f,"fire_points":[%s],"trajectory":%s}',
                id, kind, vec3(p.origin), vec3(p.velocity), number(p.remaining_lifetime),
                effect_remaining, bool(p.detonated), bool(p.smoke_active), number(p.bounces),
                number(p.spawn_time), number(p.detonate_time), number(p.expire_time),
                table.concat(fires, ","), trajectory)
        end
    end
    for id in pairs(trajectory_cache) do if not alive[id] then trajectory_cache[id] = nil end end
    for id in pairs(effect_cache) do if not alive[id] then effect_cache[id] = nil end end
    return '[' .. table.concat(output, ',') .. ']'
end

local function encode_frame(frame)
    local camera = frame.camera or {}
    local local_player = frame.local_player or {}
	local bomb = frame.bomb or {}
	local bomb_origin = bomb_position(frame, bomb)
    local lower = overview and overview.has_lower and camera.origin
        and number(camera.origin.z) <= number(overview.lower_altitude_max)
    return string.format(
        '{"version":2,"sequence":%d,"timestamp_us":%d,"connected":%s,"map":%s,"layer":%s,' ..
        '"camera":{"origin":%s,"angles":%s,"fov":%.2f},"game_time":%.3f,' ..
		'"local":{"name":%s,"team":%d,"alive":%s,"hp":%d,"origin":%s,"angles":%s},"players":%s,"projectiles":%s,' ..
		'"bomb":{"active":%s,"planted":%s,"origin":%s,"time":%.2f,"defusing":%s,"defuse":%.2f,"site":%d,"damage":%d}}',
        number(frame.sequence), number(frame.timestamp_us), bool(frame.connected), json_string(map_name),
        json_string(lower and "lower" or "primary"), vec3(camera.origin), vec3(camera.angles),
		number(camera.fov), number(local_player.game_time), json_string(local_player.name), number(local_player.team), bool(local_player.alive), number(local_player.health),
		vec3(camera.origin), vec3(camera.angles), encode_players(frame), encode_projectiles(frame), bool(bomb_origin ~= nil), bool(bomb.planted), vec3(bomb_origin), number(bomb.time_remaining),
        bool(bomb.being_defused), number(bomb.defuse_remaining), number(bomb.site), number(bomb.predicted_damage))
end

local function start_server()
    if running then return end
	if not ensure_embedded_assets() then
		status = "Embedded web assets unavailable"
		set_copy_action("Unavailable")
		return
	end
	if not ensure_weapon_font() then
		status = "Weapon font unavailable"
		set_copy_action("Unavailable")
		return
	end
    write_stop(); os.remove(status_path); os.remove(stop_path)
    local started, launch_error = vesta.helpers.start(helper_relative,
        {"-Root", root, "-Runtime", runtime, "-Session", session,
		 "-HtmlFile", html_path})
    if not started then
        running, status = false, "Error: " .. tostring(launch_error or "helper launch failed")
        set_copy_action("Unavailable")
        return
    end
    running, status = true, "Starting local server"
    set_copy_action("Starting...")
end

vesta.events.on("tick", function(delta)
    delta = math.max(0, math.min(number(delta, 0.016), 0.1))
    write_accumulator = write_accumulator + delta
    status_accumulator = status_accumulator + delta
    if os.clock() >= copy_feedback_until
        and vesta.ui.consume("copy") and share_url:match('^https?://') then
        if vesta.helpers.copy_text(share_url) then
            copy_feedback_until = os.clock() + 1.2
            set_copy_action("Copied")
        end
    end
    if status_accumulator >= 0.25 then
        status_accumulator = status_accumulator % 0.25
        refresh_status()
    end
    if running and write_accumulator >= 0.05 then
        write_accumulator = write_accumulator % 0.05
        local frame = vesta.game.radar_snapshot()
        if frame then
            if normalize_map(frame.map) ~= map_name then prepare_overview(frame) end
            if number(frame.timestamp_us) >= next_write_us and not prime_one_trajectory(frame) then
                write_state(encode_frame(frame))
                next_write_us = number(frame.timestamp_us) + 50000
            end
        end
    end
end)

vesta.events.on("load", start_server)
vesta.events.on("unload", write_stop)
