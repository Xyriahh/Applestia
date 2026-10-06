# Applestia

**Liquid Glass for [Caelestia](https://github.com/caelestia-dots/shell) on Hyprland.**

Applestia is a flavour of the Caelestia shell with *Liquid Glass*: clear (not frosted) glass that bends the
light at its edges, refined controls, SF-style typography, and menus that flow out of the button that opened them.
It keeps Caelestia's layout and motion: the left bar, the screen frame, panels that open on hover, and the same
animation curves.

It installs **next to** your existing Caelestia. You can switch between the two at any time, from a card in the shell,
from the command line, or from the login screen.

- **Native liquid glass**: every pill, card and panel is a real lens rendered by the compositor, with a uniform
  edge warp, chromatic fringing and a lit rim. It's not a blurred screenshot.
- **Shapes follow the UI**: the frame, bar and panels get glass that follows their exact silhouette, including
  rounded and "flowing" shapes while they animate.
- **Built to be light on the GPU**: glass is only drawn where there is glass, the edge shape is only rebuilt
  where something changed, and unchanged frames are skipped on the GPU without stalling the CPU.
- **Safe to try**: a boot guard falls back to stock Caelestia if the plugin ever crashes Hyprland, and
  `applestia-switch safe` turns everything off.

## Requirements

Tested with:

| | Version |
|---|---|
| Hyprland | **0.56.x** (Lua config) |
| Caelestia shell | 2.3.x, with the Caelestia Hyprland dots (`~/.config/caelestia/hypr-user.lua`) |
| Quickshell | 0.3.x |
| Qt | 6.11 |

You'll need the build tools. On Arch:

```sh
sudo pacman -S --needed base-devel cmake pkgconf wayland pixman libdrm qt6-base qt6-declarative qt6-wayland
```

The Hyprland development headers ship with the `hyprland` package on Arch (`pkg-config --modversion hyprland`
should print your version). Any GPU with OpenGL ES 3.2 gets every optimisation. Older GPUs still work; they just skip
the GPU-side shortcuts.

## Install

```sh
git clone https://github.com/Xyriahh/Applestia.git
cd Applestia
./install.sh              # add --sessions to also get login-screen entries (asks for sudo)
applestia-switch applestia
```

`install.sh` does the following, all per user and without root:

1. It builds the glass plugin (a fork of [hyprglass](https://github.com/hyprnux/hyprglass)) against *your*
   Hyprland and installs it to `~/.local/lib/applestia/`. It never overwrites a copy Hyprland has loaded.
2. It builds the `Applestia.Glass` Qt module into `~/.local/lib/qt6/qml/`.
3. It installs the Applestia shell to `~/.config/quickshell/flavours/applestia` and keeps your Caelestia as the
   `caelestia` flavour. A local copy is moved to `flavours/caelestia`; a packaged shell is linked from `/etc/xdg`.
   `~/.config/quickshell/caelestia` then becomes a symlink to the active flavour.
4. It adds one line to `~/.config/caelestia/hypr-user.lua` (with a backup) to load `applestia-hypr.lua`.
5. It installs `applestia-switch`, `applestia-session` and `applestia-install-plugin` to `~/.local/bin`.

Nothing changes until you run `applestia-switch applestia`. That loads the plugin, applies the glass settings and
restarts the shell. **Log out and back in once afterwards**, so Hyprland starts with the plugin from the beginning.

## Switching

| From | To Applestia | Back to Caelestia |
|---|---|---|
| Terminal / SSH | `applestia-switch applestia` | `applestia-switch caelestia` |
| Inside the shell | Utilities panel (bottom right) → **Shell style** → Applestia | same card → Caelestia |
| Login screen (`--sessions`) | **Hyprland (Applestia)** | **Hyprland (Caelestia)** |

`applestia-switch status` shows the current state.

## If something goes wrong

- **`applestia-switch safe`** switches back to Caelestia, and the plugin won't load at the next Hyprland start.
  `applestia-switch applestia` turns it back on.
- **Boot guard:** if Hyprland crashes while the plugin is loaded, or a start didn't finish, the next start skips the
  plugin automatically, runs plain Caelestia and shows a notification.
- `hyprctl hyprglass status` and `hyprctl hyprglass stats` show what the plugin is doing.
- After a Hyprland update, run `./install.sh` again, because the plugin must be rebuilt for each Hyprland version.

## Update

```sh
cd Applestia && git pull && ./install.sh
```

Your previous Applestia flavour is kept as `~/.config/quickshell/flavours/.applestia.old.<time>`. The glass settings
file is backed up if you changed it.

## Uninstall

```sh
./uninstall.sh
```

It switches back to Caelestia, puts your original Caelestia back where it was, removes the hook line and every
installed file. Login-screen entries are removed only if this install added them.

## Tuning

The glass materials live in `~/.config/caelestia/applestia-glass.lua`, as presets for blur, refraction, rim light
and tint. You can try changes live:

```sh
hyprctl eval 'hl.plugin.hyprglass.preset("applestia", { inherits = "pomme", refraction_strength = 1.4 })'
```

## How it works

```
Quickshell (Applestia shell)                 Hyprland + glass plugin
────────────────────────────                 ───────────────────────────────────────────
panels, bar, frame  ── blur region ───────▶  panel glass: shape from the shell's alpha
                                              (distance field, rebuilt only where changed)
pills, cards, menus ── applestia-glass- ───▶  native lenses: exact rounded-rect optics
  (Applestia.Glass)     shapes-v1 protocol     per control, drawn under the shell's pixels
```

- **Panel glass** samples and blurs the desktop behind the shell once, keeps it where the glass can see it, and
  refracts it along the silhouette of the shell's own pixels.
- **Native lenses** (the `Applestia.Glass` QML module) tell the compositor the exact geometry of each control, so
  every pill and card gets the same physically shaped lens instead of an approximation.
- **Performance:** draws are limited to 16×16 tiles that actually contain glass. Edge shapes are rebuilt only near
  pixels that changed, and the GPU decides that by itself through indirect draws. Every optimisation was checked
  against a full rebuild with `plugin:hyprglass:debug:verify`, and none of them changes the picture.

## Repository layout

| Path | What | Licence |
|---|---|---|
| `shell/` | The Applestia shell, a modified Caelestia shell | GPL-3.0 |
| `plugin/` | The glass plugin, a fork of hyprglass | BSD-3-Clause |
| `module/` | `Applestia.Glass`, the Qt/QML client for native lenses | GPL-3.0 |
| `hypr/` | Hyprland side: plugin loading, boot guard, glass presets | GPL-3.0 |
| `bin/`, `sessions/` | Switch scripts and login-screen sessions | GPL-3.0 |

## Credits

- [Caelestia](https://github.com/caelestia-dots/shell): the shell this is built on.
- [hyprglass](https://github.com/hyprnux/hyprglass) by Jeremy Trufier: the glass plugin this forks.
- [ShojiWM](https://github.com/bea4dev/ShojiWM): the silhouette-glass approach (distance field from alpha) and lens
  profile ideas (MIT).

Applestia is a fan project inspired by Apple's design language. It isn't affiliated with or endorsed by Apple.
