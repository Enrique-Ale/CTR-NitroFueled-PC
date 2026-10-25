#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Merge loose-file mods through links, preserving the original game and mod files."""
import argparse
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile

# Top-level data folders of CTR's game folder: archives/ holds the IGA archives (*.pak) and the
# Bink videos. The game's own folders are added at run time (game_folders), so a mod for any
# other folder the dump has is found too.
GAME_FOLDERS = {'archives'}
# Folders a dump or a mod's download puts around the game's folders.
WRAPPERS = ('app0', 'CUSA13795', 'CUSA14876', 'Image0')
# The executable and system files: never replaced by a mod (patches go through patches.py).
SYSTEM = {'eboot.bin', 'sce_module', 'sce_sys'}
# Readme, pictures and notes that come with a mod and stay out of the game.
EXTRAS = ('.txt', '.md', '.jpg', '.jpeg', '.png', '.gif', '.webp', '.ini', '.json', '.url', '.pdf')


def game_folders(game):
    """GAME_FOLDERS and the game folder's own data folders, case-folded."""
    folders = set(GAME_FOLDERS)
    for entry in (Path(game).iterdir() if game and Path(game).is_dir() else ()):
        if entry.is_dir() and entry.name.casefold() not in SYSTEM:
            folders.add(entry.name.casefold())
    return folders


def child(folder, name):
    """`name` inside `folder`, matching an existing entry case-insensitively (mods made on
    Windows often differ from the game's names)."""
    folder = Path(folder)
    for entry in (folder.iterdir() if folder.is_dir() else ()):
        if entry.name.casefold() == name.casefold():
            return entry
    return folder / name


def content_root(folder, folders=GAME_FOLDERS):
    """(root, prefix): where a mod's files are and the game path they go to, or None.

    Accepted layouts: the game's folders (<mod>/archives/...), the same inside app0, CUSA13795,
    CUSA14876 or Image0 (<mod>/CUSA13795/archives/...), bare archives (<mod>/*.pak, which go to
    archives/), and one wrapper folder around any of these (a download extracted into a folder of
    its name, with a readme or pictures beside it)."""
    folder = Path(folder)
    if not folder.is_dir():
        return None
    for wrapper in ('', *WRAPPERS):
        base = child(folder, wrapper) if wrapper else folder
        if any(child(base, name).is_dir() for name in folders):
            return base, ''
    entries = [e for e in folder.iterdir() if not e.name.startswith('.')]
    files = [e for e in entries if e.is_file() and e.suffix.casefold() not in EXTRAS]
    if any(e.suffix.casefold() == '.pak' for e in files):
        return folder, 'archives'
    subfolders = [e for e in entries if e.is_dir()]
    if len(subfolders) == 1 and not files:
        return content_root(subfolders[0], folders)
    return None


def discover(root, folders=GAME_FOLDERS):
    """Named mods: folders of `root` with an accepted layout (content_root)."""
    root = Path(root)
    if not root.is_dir():
        return []
    return sorted((p.name for p in root.iterdir() if p.is_dir() and content_root(p, folders)),
                  key=lambda name: (name.casefold(), name))


def selected(root, config, folders=GAME_FOLDERS):
    available = discover(root, folders)
    if not config or not Path(config).is_file():
        return available
    settings = json.loads(Path(config).read_text())
    disabled_names = settings.get('disabled', [])
    if not isinstance(disabled_names, list) or not all(isinstance(n, str) for n in disabled_names):
        raise ValueError('Disabled mods must be a list of folder names')
    disabled = set(disabled_names)
    order = settings.get('order', [])
    if not isinstance(order, list) or not all(isinstance(n, str) for n in order):
        raise ValueError('Mod order must be a list of folder names')
    # New folders are enabled automatically and appended in alphabetical order.
    return list(dict.fromkeys(n for n in [*order, *available]
                             if n in available and n not in disabled))


def mod_files(folder, folders=GAME_FOLDERS):
    """(game path, source) of every file a mod replaces or adds in the game's data folders."""
    layout = content_root(folder, folders)
    if not layout:
        raise ValueError(f'{folder}: expected archives/ (or *.pak files) inside the mod folder')
    root, prefix = layout
    root = root.resolve()
    for directory, subfolders, files in os.walk(root, followlinks=False):
        directory = Path(directory)
        for name in [*subfolders, *files]:
            if (directory / name).is_symlink():
                raise ValueError(f'Mod symlinks are unsupported: {directory / name}')
        for name in sorted(files):
            source = directory / name
            relative = source.relative_to(root)
            if relative.parts[0].casefold() in SYSTEM:
                raise ValueError(f'Executable/system replacement is unsupported: {source}')
            if prefix:
                # Bare archives: only the mod folder's own *.pak files go to archives/.
                if len(relative.parts) > 1 or source.suffix.casefold() != '.pak':
                    continue
                relative = Path(prefix) / relative
            # Readme/metadata stay outside the mounted game. This loader handles assets;
            # executable patches use the existing patch compiler, with address validation.
            elif len(relative.parts) == 1 or relative.parts[0].casefold() not in folders:
                continue
            if not source.is_file():
                raise ValueError(f'Not a regular mod file: {source}')
            yield relative, source


# Windows without the symlink privilege (Developer Mode off): directories become junctions,
# files hard links (copies across volumes). Those are not symlinks, so the overlay remembers
# what it linked and where to.
LINKED = {}


def make_link(link, target):
    try:
        link.symlink_to(target, target_is_directory=target.is_dir())
        return
    except OSError:
        if os.name != 'nt':
            raise
    if target.is_dir():
        import _winapi
        _winapi.CreateJunction(str(target), str(link))
    else:
        try:
            os.link(target, link)
        except OSError:
            shutil.copy2(target, link)
    LINKED[str(link)] = target


def symlinks_work(directory):
    probe = Path(tempfile.mkdtemp(prefix='link-test-', dir=directory))
    try:
        (probe / 'link').symlink_to(probe, target_is_directory=True)
        return True
    except OSError:
        return False
    finally:
        shutil.rmtree(probe, ignore_errors=True)


def is_link(path):
    return path.is_symlink() or str(path) in LINKED or (hasattr(path, 'is_junction') and path.is_junction())


def link_target(path):
    return LINKED.get(str(path)) or path.resolve(strict=True)


def remove_link(path):
    if hasattr(path, 'is_junction') and path.is_junction():
        os.rmdir(path)
    else:
        path.unlink()
    LINKED.pop(str(path), None)


def expand(directory):
    """Materialize one directory level; never write through a directory link."""
    if is_link(directory):
        target = link_target(directory)
        if not target.is_dir():
            raise ValueError(f'File/directory conflict at {directory.name}')
        remove_link(directory)
        directory.mkdir()
        for entry in target.iterdir():
            make_link(directory / entry.name, entry)
    elif directory.exists() and not directory.is_dir():
        raise ValueError(f'File/directory conflict at {directory.name}')
    else:
        directory.mkdir(exist_ok=True)


def build_overlay(game, out, mods):
    game = Path(game).resolve(strict=True)
    folders = game_folders(game)
    replacements = []
    owners = {}
    for name, root in mods:
        count = 0
        for relative, source in mod_files(root, folders):
            key = relative.as_posix().casefold()
            if key in owners:
                print(f'Mods: {relative}: {owners[key]} -> {name}', file=sys.stderr)
            owners[key] = name
            replacements.append((relative, source))
            count += 1
        print(f'Mods: {name}: {count} files', file=sys.stderr)
    if not replacements:
        return game
    out = Path(out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    if os.name == 'nt' and not symlinks_work(out) and out.drive.casefold() != game.drive.casefold():
        # Hard links only reach files on the same volume: the overlay goes beside the game.
        out = game.parent
    overlay = Path(tempfile.mkdtemp(prefix='mod-game-', dir=out))
    try:
        for entry in game.iterdir():
            make_link(overlay / entry.name, entry)
        replaced = added = 0
        for relative, source in replacements:
            # Each component takes the game's spelling when it exists in another case.
            parent = overlay
            for part in relative.parts[:-1]:
                parent = child(parent, part)
                expand(parent)
            destination = child(parent, relative.parts[-1])
            if is_link(destination):
                replaced += link_target(destination).is_relative_to(game)
                remove_link(destination)
            elif destination.exists():
                raise ValueError(f'File/directory conflict: {relative}')
            else:
                added += 1
            make_link(destination, source)
        print(f'Mods: {replaced} game files replaced, {added} added', file=sys.stderr)
        return overlay
    except BaseException:
        shutil.rmtree(overlay)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--mods-dir', required=True, type=Path)
    parser.add_argument('--config', type=Path)
    parser.add_argument('--enabled', choices=('0', '1'), default='1')
    args = parser.parse_args()
    layers = []
    folders = game_folders(args.game)
    if args.enabled == '1':
        # shadPS4's loose overlay convention, beside the original game.
        legacy = Path(str(args.game.resolve()) + '-mods')
        if legacy.is_dir():
            layers.append((legacy.name, legacy))
        # A directly selected loose overlay (the mods folder holds archives/ itself) is also accepted.
        if any(child(args.mods_dir, name).is_dir() for name in folders):
            layers.append((args.mods_dir.name, args.mods_dir))
        else:
            for name in selected(args.mods_dir, args.config, folders):
                layers.append((name, args.mods_dir / name))
    print(build_overlay(args.game, args.out, layers))


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, TypeError) as error:
        print(f'Mods: {error}', file=sys.stderr)
        sys.exit(1)
