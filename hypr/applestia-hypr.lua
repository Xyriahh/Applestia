-- Applestia: Hyprland side of the flavour switch.
-- Loaded at the end of hypr-user.lua through pcall, and re-run by applestia-switch
-- via `hyprctl eval 'dofile(...)'`. In caelestia mode it does nothing.

local home   = os.getenv("HOME")
local state  = home .. "/.local/state/applestia"
local plugin = home .. "/.local/lib/applestia/hyprglass.so"

local function read_first_line(path)
    local f = io.open(path)
    if not f then return nil end
    local line = f:read("*l")
    f:close()
    return line
end

local function exists(path)
    local f = io.open(path)
    if f then f:close() return true end
    return false
end

local function write(path, content)
    local f = io.open(path, "w")
    if f then
        f:write(content or "")
        f:close()
    end
end

local function notify(msg)
    hl.exec_cmd("notify-send -a Applestia 'Applestia' '" .. msg .. "'")
end

-- Newest Hyprland crash report, as an mtime (0 if none).
local function newest_crash_report()
    local p = io.popen("stat -c %Y " .. home .. "/.cache/hyprland/hyprlandCrashReport*.txt 2>/dev/null | sort -n | tail -1")
    if not p then return 0 end
    local t = tonumber(p:read("*l") or "") or 0
    p:close()
    return t
end

local function fall_back(reason)
    write(state .. "/mode", "caelestia\n")
    write(state .. "/skip-plugin", reason .. "\n")
    -- The shell is started from execs.lua through the caelestia symlink; point it back.
    os.execute("ln -sfn flavours/caelestia " .. home .. "/.config/quickshell/caelestia")
    notify(reason .. " Running Caelestia; use applestia-switch applestia to retry.")
end

-- Applestia v2: the shell's native glass module (Applestia.Glass) lives here.
-- Harmless for stock Caelestia (it never imports it).
hl.env("QML_IMPORT_PATH", home .. "/.local/lib/qt6/qml")
-- applestia-switch lives in ~/.local/bin: make it reachable from the shell's "Shell style" card.
do
    local path = os.getenv("PATH") or "/usr/bin"
    if not path:find(home .. "/.local/bin", 1, true) then
        hl.env("PATH", home .. "/.local/bin:" .. path)
    end
end

local mode = read_first_line(state .. "/mode")
if mode ~= "applestia" then return end

-- Boot guard. Two ways the previous plugin session can have died:
--  * a crash at any time: a crash report newer than the last plugin load;
--  * a hang/crash during startup: the `loading` marker is still there.
if not hl.plugin.hyprglass then
    local loaded_at = tonumber(read_first_line(state .. "/plugin-loaded-at") or "") or 0
    if exists(state .. "/skip-plugin") or not exists(state .. "/plugin-approved") then
        fall_back("Glass plugin is disabled (safe mode or not approved).")
        return
    end
    if exists(state .. "/loading") then
        os.remove(state .. "/loading")
        fall_back("Glass plugin skipped: the previous session did not start cleanly.")
        return
    end
    if loaded_at > 0 and newest_crash_report() > loaded_at then
        fall_back("Glass plugin skipped: Hyprland crashed while it was loaded.")
        return
    end
    if not exists(plugin) then
        fall_back("Glass plugin not installed at " .. plugin .. ".")
        return
    end

    -- hl.plugin.load / hl.exec_cmd do nothing inside `hyprctl eval` (only setters
    -- apply there), so loading always goes through hyprctl from a shell. The glass
    -- settings are applied right after the load, before the shell starts drawing.
    write(state .. "/loading", "")
    write(state .. "/plugin-loaded-at", tostring(os.time()) .. "\n")
    hl.on("hyprland.start", function()
        hl.exec_cmd("sh -c 'hyprctl plugin load " .. plugin .. " && hyprctl eval \"dofile(\\\"" .. home .. "/.config/caelestia/applestia-glass.lua\\\")\"; sleep 90; rm -f " .. state .. "/loading'")
    end)
    return
end

if not hl.plugin.hyprglass then return end

local ok, err = pcall(dofile, home .. "/.config/caelestia/applestia-glass.lua")
if not ok then notify("applestia-glass.lua failed: " .. tostring(err)) end
