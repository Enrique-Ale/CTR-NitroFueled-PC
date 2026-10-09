#!/usr/bin/env bash
# Adds the launcher to the application menu (~/.local/share/applications) for this checkout.
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
dest=${XDG_DATA_HOME:-$HOME/.local/share}/applications
mkdir -p "$dest"
sed "s|@LAUNCHER@|$here/bb-launcher.sh|" "$here/io.github.sacredfire.Launcher.desktop.in" \
    > "$dest/io.github.sacredfire.Launcher.desktop"
echo "Installed $dest/io.github.sacredfire.Launcher.desktop"
