#!/usr/bin/env python3
"""The game files this port runs, keyed on the executable's loaded image.

Profile: Crash Team Racing Nitro-Fueled CUSA13795 with the 1.21 update merged in (the patches in
patches/CrashTeamRacing.xml use that executable's addresses). The check compares the loaded
executable image, whose hash is the same whatever tool dumped it (the SELF headers around it
differ). BB_SKIP_GAME_CHECK=1 skips it. bbport's Bloodborne profile was removed; git tag
bloodborne-support is the last tree that has it.
"""
import hashlib
import os
import sys
from pathlib import Path

# image sha256 -> profile. `name` is what the runtime and the GPU menu read (BB_GAME_PROFILE);
# `modules` are the sce_module PRXs link_modules.py links natively; `patches` says whether
# patches.py has a patch file for it.
SUPPORTED = {
    '2f2115893ac6656925d20a0221f118cff398fbd754cb9c10154451411dbcf8e6': dict(
        name='ctr', title='CUSA13795', version='01.21', game='Crash Team Racing Nitro-Fueled', update='CUSA13795-patch',
        modules=('libc.prx', 'libSceFios2.prx', 'libSceNpToolkit2.prx'), patches=True),
}
BY_TITLE = {p['title']: p for p in SUPPORTED.values()}
SUPPORTED_TITLE = 'CUSA13795'
SUPPORTED_VERSION = '01.21'
SUPPORTED_IMAGE = '2f2115893ac6656925d20a0221f118cff398fbd754cb9c10154451411dbcf8e6'


def image_sha256(game):
    """The SHA-256 of eboot.bin's loaded image (its loadable segments at their addresses)."""
    from prepare import parse_self, span  # same directory
    elf, header, ph, _segments, _missing = parse_self((Path(game) / 'eboot.bin').read_bytes())
    loads = [p for p in ph if p['type'] in (1, 0x61000010)]
    image = bytearray(max(p['vaddr'] + p['memsz'] for p in loads))
    for p in loads:
        image[p['vaddr']:p['vaddr'] + p['filesz']] = span(elf, p['offset'], p['filesz'])
    return hashlib.sha256(image).hexdigest()


def profile(game, image_hash=None):
    """The profile dict for a supported game folder, else None (BB_SKIP_GAME_CHECK=1: a profile
    guessed from param.sfo's TITLE_ID when one exists, so run.sh can still branch)."""
    try:
        found = SUPPORTED.get(image_hash or image_sha256(game))
    except (OSError, ValueError, IndexError, StopIteration, KeyError):
        found = None
    if found or os.environ.get('BB_SKIP_GAME_CHECK') != '1':
        return found
    from prepare import sfo
    try:
        info = sfo((Path(game) / 'sce_sys/param.sfo').read_bytes())
    except (OSError, ValueError, IndexError):
        return None
    return BY_TITLE.get(info.get('TITLE_ID'))


def problem(game, image_hash=None):
    """None for a supported game, else (kind, title, version): kind is 'missing_update' (base
    game or an older update), 'wrong_eboot' (param.sfo names the supported update, eboot.bin is
    another version), 'other_title' (another edition, region or game) or 'unreadable'."""
    if os.environ.get('BB_SKIP_GAME_CHECK') == '1':
        return None
    from prepare import sfo
    try:
        info = sfo((Path(game) / 'sce_sys/param.sfo').read_bytes())
    except (OSError, ValueError, IndexError):
        info = {}
    title, version = info.get('TITLE_ID', '?'), info.get('APP_VER', '?')
    try:
        if (image_hash or image_sha256(game)) in SUPPORTED:
            return None
    except (OSError, ValueError, IndexError, StopIteration, KeyError):
        return 'unreadable', title, version
    if title not in BY_TITLE:
        return 'other_title', title, version
    if version != BY_TITLE[title]['version']:
        return 'missing_update', title, version
    return 'wrong_eboot', title, version


def explain(kind, title, version):
    """What is wrong and what to do, for the log (English)."""
    found = f'Found {title} version {version} (sce_sys/param.sfo).'
    p = BY_TITLE.get(title, BY_TITLE[SUPPORTED_TITLE])
    supported = ', '.join(f"{q['game']} {q['title']} {q['version']}" for q in SUPPORTED.values())
    return {
        'missing_update': f"{found} The port needs the {p['version']} update merged into the game folder: copy "
                          f"everything from the dumped {p['version']} update (e.g. {p['update']}) into the "
                          'game folder, replacing files (eboot.bin and sce_sys too).',
        'wrong_eboot': f"{found} param.sfo is from {p['version']} but eboot.bin is not: copy eboot.bin from "
                       f"the dumped {p['version']} update into the game folder, replacing the old one.",
        'other_title': f'{found} Supported: {supported}; other editions and regions have a different executable.',
        'unreadable': f'{found} eboot.bin could not be read as a decrypted PS4 executable: dump '
                      'the game and its update again.',
    }[kind]


if __name__ == '__main__':
    # game_check.py GAME_DIR: prints the profile name (run.sh branches on it), or fails with the reason.
    game = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('.')
    found = problem(game)
    if found:
        sys.exit('Unsupported game files: ' + explain(*found))
    p = profile(game)
    print(p['name'] if p else 'unknown')
