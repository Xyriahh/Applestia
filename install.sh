#!/bin/sh
# Applestia installer: adds the Applestia (Liquid Glass) flavour next to an
# existing Caelestia install. Your Caelestia shell stays installed and selectable.
#
#   ./install.sh               build + install for the current user
#   ./install.sh --sessions    also add "Hyprland (Applestia)" / "Hyprland (Caelestia)"
#                              to the login screen (asks for sudo)
#   ./install.sh --no-build    reuse plugin/hyprglass.so and module/build from a previous run
#
# Nothing is switched on by itself: afterwards run `applestia-switch applestia`.
set -eu

REPO=$(cd "$(dirname "$0")" && pwd)
BIN=$HOME/.local/bin
LIB=$HOME/.local/lib/applestia
QML_ROOT=$HOME/.local/lib/qt6/qml
QS=$HOME/.config/quickshell
CAEL=$HOME/.config/caelestia
STATE=$HOME/.local/state/applestia

SESSIONS=0
BUILD=1
for arg in "$@"; do
    case $arg in
        --sessions) SESSIONS=1 ;;
        --no-build) BUILD=0 ;;
        -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

say()  { printf '\033[1;36m::\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m!!\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31mxx\033[0m %s\n' "$*" >&2; exit 1; }

# ── checks ──────────────────────────────────────────────────────────────────
for cmd in hyprctl qs pkg-config make cmake c++ wayland-scanner; do
    command -v "$cmd" >/dev/null 2>&1 || die "missing '$cmd' (see README: Requirements)"
done
pkg-config --exists hyprland || die "Hyprland development files not found (pkg-config hyprland)"
HYPR_VERSION=$(pkg-config --modversion hyprland)
case $HYPR_VERSION in
    0.56*) ;;
    *) warn "built and tested on Hyprland 0.56.x; you have $HYPR_VERSION — the plugin may not compile or load" ;;
esac

if [ -L "$QS/caelestia" ] || [ -d "$QS/caelestia" ] || [ -d /etc/xdg/quickshell/caelestia ]; then :; else
    die "no Caelestia shell found (~/.config/quickshell/caelestia or /etc/xdg/quickshell/caelestia)"
fi
[ -f "$CAEL/hypr-user.lua" ] || warn "~/.config/caelestia/hypr-user.lua not found: you will add one line yourself (printed at the end)"

# ── build ───────────────────────────────────────────────────────────────────
if [ $BUILD = 1 ]; then
    say "building the glass plugin (hyprglass fork) against Hyprland $HYPR_VERSION"
    make -C "$REPO/plugin" -j"$(nproc)" HYPRGLASS_VERSION="applestia" >/dev/null
    say "building the Applestia.Glass Qt module"
    cmake -S "$REPO/module" -B "$REPO/module/build" -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$HOME/.local" -DGLASS_QML_INSTALL_DIR:STRING=lib/qt6/qml >/dev/null
    cmake --build "$REPO/module/build" -j"$(nproc)" >/dev/null
fi
[ -f "$REPO/plugin/hyprglass.so" ] || die "plugin/hyprglass.so missing (run without --no-build)"
[ -d "$REPO/module/build" ] || die "module/build missing (run without --no-build)"

# ── install ─────────────────────────────────────────────────────────────────
say "installing scripts to $BIN"
mkdir -p "$BIN"
for f in applestia-switch applestia-install-plugin applestia-session; do
    install -m 755 "$REPO/bin/$f" "$BIN/$f"
done

say "installing the plugin to $LIB (atomic: never overwrites a loaded copy)"
"$BIN/applestia-install-plugin" "$REPO/plugin/hyprglass.so" >/dev/null

say "installing the Qt module to $QML_ROOT/Applestia/Glass"
cmake --install "$REPO/module/build" >/dev/null

say "installing the shell flavour"
mkdir -p "$QS/flavours"
if [ -L "$QS/caelestia" ]; then
    : # already switchable (re-install / upgrade)
elif [ -d "$QS/caelestia" ]; then
    # a local Caelestia copy: keep it as the "caelestia" flavour
    [ -e "$QS/flavours/caelestia" ] && die "$QS/flavours/caelestia already exists; move it away first"
    mv "$QS/caelestia" "$QS/flavours/caelestia"
    ln -s flavours/caelestia "$QS/caelestia"
else
    # the packaged Caelestia (/etc/xdg): point the "caelestia" flavour at it
    [ -e "$QS/flavours/caelestia" ] || ln -s /etc/xdg/quickshell/caelestia "$QS/flavours/caelestia"
    ln -s flavours/caelestia "$QS/caelestia"
fi
if [ -e "$QS/flavours/applestia" ]; then
    backup="$QS/flavours/.applestia.old.$(date +%s)"
    mv "$QS/flavours/applestia" "$backup"
    say "previous Applestia flavour kept at $backup"
fi
cp -a "$REPO/shell" "$QS/flavours/applestia"

say "installing the Hyprland side to $CAEL"
mkdir -p "$CAEL"
cp "$REPO/hypr/applestia-hypr.lua" "$CAEL/applestia-hypr.lua"
if [ -f "$CAEL/applestia-glass.lua" ] && ! cmp -s "$REPO/hypr/applestia-glass.lua" "$CAEL/applestia-glass.lua"; then
    cp "$CAEL/applestia-glass.lua" "$CAEL/applestia-glass.lua.bak.$(date +%s)"
fi
cp "$REPO/hypr/applestia-glass.lua" "$CAEL/applestia-glass.lua"
HOOK='pcall(dofile, os.getenv("HOME") .. "/.config/caelestia/applestia-hypr.lua")'
if [ -f "$CAEL/hypr-user.lua" ]; then
    if ! grep -qF 'applestia-hypr.lua' "$CAEL/hypr-user.lua"; then
        cp "$CAEL/hypr-user.lua" "$CAEL/hypr-user.lua.bak.$(date +%s)"
        printf '\n-- Applestia flavour (Liquid Glass). pcall: a broken file must never break the config.\n%s\n' "$HOOK" >>"$CAEL/hypr-user.lua"
        say "hook added to $CAEL/hypr-user.lua (backup next to it)"
    fi
fi

mkdir -p "$STATE"
[ -f "$STATE/mode" ] || echo caelestia >"$STATE/mode"
touch "$STATE/plugin-approved" # you approved the plugin by installing it; `applestia-switch safe` revokes it
rm -f "$STATE/skip-plugin"

if [ $SESSIONS = 1 ]; then
    say "adding login-screen sessions (sudo)"
    for t in applestia caelestia; do
        sed "s|@BINDIR@|$BIN|" "$REPO/sessions/hyprland-$t.desktop" | sudo tee "/usr/share/wayland-sessions/hyprland-$t.desktop" >/dev/null
    done
fi

# ── done ────────────────────────────────────────────────────────────────────
say "done."
echo
if [ ! -f "$CAEL/hypr-user.lua" ] || ! grep -qF 'applestia-hypr.lua' "$CAEL/hypr-user.lua"; then
    echo "  Add this line to the end of your Hyprland Lua config:"
    echo "    $HOOK"
    echo
fi
echo "  Turn it on:   applestia-switch applestia"
echo "  Back to stock: applestia-switch caelestia"
echo "  Something wrong after a restart? applestia-switch safe"
echo
echo "  Tip: log out and back in once after switching, so Hyprland starts with the plugin."
