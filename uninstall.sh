#!/bin/sh
# Remove Applestia and give the shell back to Caelestia.
# The loaded plugin keeps running until Hyprland restarts; it is not unloaded live.
set -eu

BIN=$HOME/.local/bin
QS=$HOME/.config/quickshell
CAEL=$HOME/.config/caelestia

say() { printf '\033[1;36m::\033[0m %s\n' "$*"; }

if [ -x "$BIN/applestia-switch" ]; then
    say "switching back to Caelestia"
    "$BIN/applestia-switch" safe || true
fi

say "removing the Applestia flavour"
rm -rf "$QS/flavours/applestia"
if [ -L "$QS/caelestia" ]; then
    rm "$QS/caelestia"
    if [ -L "$QS/flavours/caelestia" ]; then
        rm "$QS/flavours/caelestia"          # pointed at the packaged shell: nothing to restore
    elif [ -d "$QS/flavours/caelestia" ]; then
        mv "$QS/flavours/caelestia" "$QS/caelestia" # your local Caelestia copy goes back in place
    fi
    rmdir "$QS/flavours" 2>/dev/null || true
fi

say "removing the Hyprland hook and files"
if [ -f "$CAEL/hypr-user.lua" ]; then
    sed -i '/Applestia flavour (Liquid Glass)/d; /applestia-hypr\.lua/d' "$CAEL/hypr-user.lua"
fi
rm -f "$CAEL/applestia-hypr.lua" "$CAEL/applestia-glass.lua"

say "removing the plugin, Qt module and scripts"
rm -rf "$HOME/.local/lib/qt6/qml/Applestia"
rm -f "$BIN/applestia-switch" "$BIN/applestia-install-plugin" "$BIN/applestia-session"
# The plugin file may still be mapped by Hyprland: unlinking is safe (the running copy stays intact).
rm -rf "$HOME/.local/lib/applestia" "$HOME/.local/state/applestia"

# Login-screen sessions: only the ones this user's install.sh --sessions added.
for t in applestia caelestia; do
    f="/usr/share/wayland-sessions/hyprland-$t.desktop"
    if [ -f "$f" ] && grep -qF "Exec=$BIN/applestia-session" "$f"; then
        say "removing login-screen session hyprland-$t (sudo)"
        sudo rm -f "$f"
    fi
done

say "done. Restart the shell (caelestia shell -d) or log out and in."
