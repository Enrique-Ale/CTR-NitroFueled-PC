#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Builds the Windows portable ZIP from a finished build (out/sacredfire.exe).

    C:\\msys64\\clang64\\bin\\python.exe packaging/windows/make_portable.py [--version v0.1.1]

Needs MSYS2 (CLANG64) for the DLLs and for pacman, which tells THIRD-PARTY.txt which package,
version and license each bundled DLL comes from. Output: dist/CTR-NitroFueled-PC-portable-windows.zip
(+ the unpacked folder next to it, and its SHA256). Nothing from the user's own machine goes in:
no user/ folder, no saves, no logs, no personal bbport.ini (the defaults are
packaging/windows/portable/bbport.ini).
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]
PORTABLE = ROOT / 'packaging' / 'windows' / 'portable'
MSYS2 = Path(os.environ.get('MSYS2_ROOT', r'C:\msys64'))
CLANG_BIN = MSYS2 / 'clang64' / 'bin'
NAME = 'CTR-NitroFueled-PC-portable'
# Windows' own DLLs: never bundled.
SYSTEM_DLLS = {'kernel32', 'user32', 'gdi32', 'advapi32', 'shell32', 'ole32', 'oleaut32', 'ws2_32',
               'msvcrt', 'ntdll', 'bcrypt', 'bcryptprimitives', 'crypt32', 'secur32', 'winmm',
               'imm32', 'setupapi', 'version', 'shlwapi', 'comdlg32', 'comctl32', 'rpcrt4',
               'dwmapi', 'd3d11', 'dxgi', 'd3d9', 'd3d12', 'dxva2', 'mf', 'mfplat', 'mfuuid',
               'mfreadwrite', 'uxtheme', 'cfgmgr32', 'hid', 'opengl32', 'vulkan-1', 'iphlpapi',
               'mswsock', 'dnsapi', 'ncrypt', 'usp10', 'shcore', 'cabinet', 'wldap32', 'normaliz',
               'avrt', 'msimg32', 'propsys', 'windowscodecs', 'mmdevapi', 'powrprof', 'userenv',
               'psapi', 'netapi32', 'wsock32', 'd3dcompiler_47', 'gdiplus', 'd2d1', 'dwrite',
               'dinput8', 'xinput1_4', 'ucrtbase', 'vcruntime140', 'msvcp140', 'winspool',
               'cryptbase', 'sspicli', 'wintrust', 'rtworkq', 'ksuser', 'wevtapi', 'ntmarta'}


def imports(path):
    """DLL names a PE file imports (llvm-objdump from the CLANG64 toolchain)."""
    out = subprocess.run([str(CLANG_BIN / 'llvm-objdump.exe'), '-p', str(path)], capture_output=True,
                         text=True, errors='replace').stdout
    return {m.group(1).lower() for m in re.finditer(r'DLL Name:\s*(\S+)', out)}


def closure(binaries):
    """Every non-system DLL the binaries need, found in the CLANG64 bin folder."""
    found, queue = {}, list(binaries)
    while queue:
        for dll in imports(queue.pop()):
            stem = dll[:-4] if dll.endswith('.dll') else dll
            if dll in found or stem in SYSTEM_DLLS or dll.startswith(('api-ms-', 'ext-ms-')):
                continue
            path = CLANG_BIN / dll
            if not path.is_file():
                sys.exit(f'{dll} is needed but is not in {CLANG_BIN}')
            found[dll] = path
            queue.append(path)
    return found


def pacman(*args):
    return subprocess.run([str(MSYS2 / 'usr' / 'bin' / 'pacman.exe'), *args], capture_output=True,
                          text=True, errors='replace').stdout


def third_party(dlls):
    """THIRD-PARTY.txt: the MSYS2 package, version, license and home page of every bundled DLL."""
    packages = {}
    for dll in sorted(dlls):
        owner = pacman('-Qo', f'/clang64/bin/{dll}').strip()
        m = re.search(r'is owned by (\S+) (\S+)', owner)
        packages.setdefault(m.group(1) if m else '(unknown)', {'version': m.group(2) if m else '', 'dlls': []})['dlls'].append(dll)
    lines = ['THIRD-PARTY SOFTWARE in bin\\',
             '=' * 60,
             'CTR-NitroFueled-PC itself is GPL-2.0-or-later (see LICENSE). The libraries below are',
             'MSYS2 CLANG64 packages, copied unchanged next to sacredfire.exe. Each keeps its own',
             'license; the package page lists the source and the build recipe',
             '(https://packages.msys2.org/ and https://github.com/msys2/MINGW-packages).', '']
    for package in sorted(packages):
        info = pacman('-Qi', package)
        field = lambda key: next((l.split(':', 1)[1].strip() for l in info.splitlines() if l.startswith(key)), '?')
        lines += [f'{package} {packages[package]["version"]}',
                  f'  license: {field("Licenses")}',
                  f'  home:    {field("URL")}',
                  f'  source:  https://packages.msys2.org/packages/{package}',
                  f'  files:   {", ".join(packages[package]["dlls"])}', '']
    lines += ['The game files are not included and are not covered by any of these licenses.',
              'Not affiliated with Sony, Activision or Beenox.', '']
    return '\r\n'.join(lines)


def write_text(path, text):
    path.write_bytes(text.replace('\r\n', '\n').replace('\n', '\r\n').encode('utf-8'))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--version', default='')
    p.add_argument('--dist', type=Path, default=ROOT / 'dist')
    a = p.parse_args()
    exe, caps = ROOT / 'out' / 'sacredfire.exe', ROOT / 'out' / 'bb-gpu-capabilities.exe'
    for f in (exe, caps):
        if not f.is_file():
            sys.exit(f'{f} not found: build with build.sh first')
    folder = a.dist / NAME
    shutil.rmtree(folder, ignore_errors=True)
    for sub in ('bin', 'scripts', 'tools', 'patches', 'out'):
        (folder / sub).mkdir(parents=True)
    shutil.copy2(exe, folder / 'bin')
    shutil.copy2(caps, folder / 'bin')
    version = a.version and f'{a.version}-windows' or (ROOT / 'out' / 'VERSION').read_text().strip()
    (folder / 'bin' / 'VERSION').write_text(version + '\n')
    dlls = closure([exe, caps])
    for path in dlls.values():
        shutil.copy2(path, folder / 'bin')
    shutil.copy2(ROOT / 'run.py', folder)
    for script in (ROOT / 'scripts').glob('*.py'):
        shutil.copy2(script, folder / 'scripts')
    for tool in ('gen_import_names.py', 'merge_update.py'):
        shutil.copy2(ROOT / 'tools' / tool, folder / 'tools')
    shutil.copy2(ROOT / 'patches' / 'CrashTeamRacing.xml', folder / 'patches')
    if (ROOT / 'out' / 'aerolib.inl').is_file():
        shutil.copy2(ROOT / 'out' / 'aerolib.inl', folder / 'out')
    for text in ('bbport.ini', '1-Mergear actualizacion.bat', 'Jugar CTR Nitro-Fueled.bat', 'LEEME.txt'):
        write_text(folder / text, (PORTABLE / text).read_text(encoding='utf-8'))
    write_text(folder / 'LICENSE', (ROOT / 'LICENSE').read_text(encoding='utf-8'))
    (folder / 'THIRD-PARTY.txt').write_bytes(third_party(dlls).encode('utf-8'))

    archive = a.dist / f'{NAME}-windows.zip'
    archive.unlink(missing_ok=True)
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for path in sorted(folder.rglob('*')):
            if path.is_file():  # forward slashes in the entry names, as the ZIP format asks
                z.write(path, f'{NAME}/{path.relative_to(folder).as_posix()}')
    digest = hashlib.sha256(archive.read_bytes()).hexdigest().upper()
    print(f'{archive}  {archive.stat().st_size / 1e6:.1f} MB  {len(dlls)} DLLs  {version}')
    print(f'SHA256 {digest}')
    (a.dist / f'{archive.name}.sha256').write_text(f'{digest}  {archive.name}\n')


if __name__ == '__main__':
    main()
