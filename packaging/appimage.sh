#!/usr/bin/env bash
# Builds dist/SacredFire-CTR-x86_64.AppImage: the port's current build (run build.sh first),
# its scripts and the launcher, bundled with their Nix closure (nix-appimage: the AppImage mounts
# its /nix/store with user namespaces, available on SteamOS and most desktops).
# Running it opens the launcher; `--play` starts the game with the launcher's saved settings.
# Data (generated files, saves, bbport.ini): ~/.local/share/sacredfire (BB_DATA_DIR).
# Needs Nix (nix bundle, flakes) and binaries built inside nix-shell, whose RUNPATHs name the store
# paths the bundle collects (a build against the system's libraries would not run elsewhere):
#   NIX_PATH=nixpkgs=$HOME/.nix-defexpr/channels/nixpkgs   # single-user Nix with a channel
#   mv out out.arch; nix-shell shell.nix --run 'BB_IN_NIX_SHELL=1 bash build.sh'
#   bash packaging/appimage.sh
#   mv out out.nix; mv out.arch out        # the everyday build goes back (out.* are ignored)
# nix bundle extracts nothing, but running the AppImage with --appimage-extract-and-run (no FUSE)
# unpacks 2.8 GB into $TMPDIR: point TMPDIR at a disk, not a small tmpfs. After editing files
# that go into the package, remove ~/.cache/nix/fetcher-cache-v*.sqlite* so the source is re-read.
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
[[ -f out/sacredfire && -f out/gpu/libbbgpu.so ]] || { echo 'Build first: bash build.sh' >&2; exit 1; }
root=$PWD
# The libraries' store paths (RUNPATH entries and their closures come along).
{
    echo '['
    for elf in out/sacredfire out/gpu/libbbgpu.so; do
        readelf -d "$elf" | sed -n 's/.*\[\(.*\)\]/\1/p' | tr ':' '\n'
    done | grep -o '^/nix/store/[^/]*' | sort -u | grep -v -- '-nix-shell$' | sed 's/.*/  "&"/'
    echo ']'
} > packaging/runtime-paths.nix
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
(cd "$work" && nix bundle --impure --bundler github:ralismark/nix-appimage \
    --expr "import $root/packaging {}")
mkdir -p dist
# The bundler names its output after the package's main program (sacredfire.AppImage).
built=$(ls "$work"/*.AppImage | head -1)
install -m755 "$built" dist/SacredFire-CTR-x86_64.AppImage
ls -lh dist/SacredFire-CTR-x86_64.AppImage
