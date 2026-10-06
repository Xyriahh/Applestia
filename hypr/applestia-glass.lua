-- Applestia glass materials and layer rules . Only runs when the
-- hyprglass plugin is loaded and the mode is applestia. Values are starting
-- points; tune live with `hyprctl eval 'hl.plugin.hyprglass.preset(...)'`.

local hg = hl.plugin.hyprglass

-- Follow Caelestia's light/dark scheme.
local theme = "dark"
local f = io.open(os.getenv("HOME") .. "/.local/state/caelestia/scheme.json")
if f then
    local s = f:read("*a")
    f:close()
    if s:find('"mode"%s*:%s*"light"') then theme = "light" end
end

hg.config({
    -- `enabled` is WINDOW glass in hyprglass: off. Applestia only glasses its own
    -- layer surfaces (layers.enabled below). Leaving it on puts glass on every window.
    enabled = false,
    default_theme = theme,
    default_preset = "applestia",
    manage_window_blur = false,
    layers = { enabled = true, mask_mode = "region", live_resample = true, live_resample_fps = 60, manage_blur = true },
})

-- Clear "Liquid Glass" (not frosted): little blur, very light tint, strong
-- lensing at the rim, crisp specular edge. The shell's own tint (QML) is faint.
hg.preset("applestia", {
    inherits = "pomme",
    blur_strength = 0.35, blur_iterations = 1,
    refraction_strength = 1.6, refraction_flow = 1.0, refraction_spread = 0.45,
    chromatic_aberration = 0.35,
    fresnel_strength = 0.35, specular_strength = 0.85, specular_angle = 330,
    bevel_strength = 0.6, bevel_size = 2.5, bevel_tint = 0.4, bevel_angle = 315,
    lens_distortion = 0.0,
    dark  = { brightness = 1.02, contrast = 1.0, saturation = 1.25, vibrancy = 0.15, adaptive_dim = 0.15, tint_color = 0x10101420 },
    light = { brightness = 1.04, contrast = 1.0, saturation = 1.2, vibrancy = 0.12, adaptive_boost = 0.1, tint_color = 0xffffff26 },
})
hg.preset("applestia_clear", {
    inherits = "applestia", blur_strength = 0.15, refraction_strength = 1.9,
    dark = { tint_color = 0x10101410 }, light = { tint_color = 0xffffff14 },
})
hg.preset("applestia_thick", {
    inherits = "applestia", blur_strength = 1.2, blur_iterations = 2, refraction_strength = 1.3,
    dark = { tint_color = 0x14141850 }, light = { tint_color = 0xf6f6f860 },
})

-- Stacked glass for controls (modules/glasslayer/ControlGlass.qml): bodies in
-- overlay layers above the drawers refract the finished panel beneath them.
-- No blur (labels underneath stay sharp), almost no tint (the QML card fill
-- already provides the frost), a strong but narrow lens at the rim, lit lip.
hg.preset("applestia_inner", {
    inherits = "applestia",
    blur_strength = 0.0, blur_iterations = 1,
    refraction_strength = 3.0, refraction_flow = 0.0, refraction_spread = 1.0,
    chromatic_aberration = 0.3,
    fresnel_strength = 0.3, specular_strength = 0.9,
    bevel_strength = 0.6, bevel_size = 1.6, bevel_tint = 0.3,
    dark  = { brightness = 1.0, contrast = 1.0, saturation = 1.0, vibrancy = 0.0, adaptive_dim = 0.0, tint_color = 0x00000000 },
    light = { brightness = 1.0, contrast = 1.0, saturation = 1.0, vibrancy = 0.0, adaptive_boost = 0.0, tint_color = 0x00000000 },
})

-- The settings window is much larger than a drawer/control: keep its outer
-- lens narrow instead of scaling a thick bezel across the whole window.
hg.preset("applestia_settings", {
    inherits = "applestia", edge_thickness = 0.008,
    refraction_strength = 0.2, refraction_spread = 0.2,
})

-- `shape = "silhouette"` is added by our fork , which also exposes
-- hg.features().silhouette == true. Stock hyprglass gets plain box glass.
local silhouette = (type(hg.features) == "function" and hg.features().silhouette) and { shape = "silhouette", silhouette_threshold = 0.04 } or {}
local function layer(ns, opts)
    for k, v in pairs(silhouette) do opts[k] = v end
    hg.layer(ns, opts)
end

layer("applestia-drawers",  { preset = "applestia", rim_width = silhouette.shape and 22 or nil })
-- One layer per (nesting level x lens size); the shell picks a size per element
-- (InnerGlass.rimSizes in the Applestia flavour: small 5, medium 8, large 12).
for _, lvl in ipairs({ 1, 2 }) do
    for name, rim in pairs({ small = 5, medium = 8, large = 12 }) do
        layer("applestia-lens-l" .. lvl .. "-" .. name, { preset = "applestia_inner", rim_width = silhouette.shape and rim or nil })
    end
end
layer("applestia-menubar",  { preset = "applestia", rim_width = silhouette.shape and 12 or nil })
layer("applestia-notifs",   { preset = "applestia_thick" })
layer("applestia-launcher", { preset = "applestia_thick" })
layer("applestia-osd",      { preset = "applestia_clear" })
hg.layer("applestia-exclusion",  { exclude = true })
hg.layer("applestia-background", { exclude = true })
hg.layer("applestia-border-exclusion", { exclude = true })
hg.layer("applestia-scrim", { exclude = true })

hl.layer_rule({ match = { namespace = "applestia-(drawers|background|notifs|launcher|osd|menubar)" }, animation = "fade" })
hl.layer_rule({ match = { namespace = "applestia-lens-.*" }, no_anim = true })
hl.layer_rule({ match = { namespace = "applestia-(exclusion|border-exclusion)" }, no_anim = true })

-- Native per-shape material (Applestia v2). May vary frost/tint/light, never lens geometry.
hg.preset("applestia_control", {
    inherits = "applestia",
    blur_strength = 0.20, blur_iterations = 1,
    specular_strength = 0.65,
    dark = { brightness = 1.0, contrast = 1.0, saturation = 1.0, adaptive_dim = 0.0, tint_color = 0x00000000 },
    light = { brightness = 1.0, contrast = 1.0, saturation = 1.0, adaptive_boost = 0.0, tint_color = 0x00000000 },
})
