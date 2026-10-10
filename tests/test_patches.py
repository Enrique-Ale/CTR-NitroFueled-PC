from paths import ROOT
import json
import struct
import tempfile
import unittest
from pathlib import Path

import patches
from patches import (EBOOT_BASE, GAME_PATCH_KEYS, compile_external, eboot_segments, encode,
                     external_patches, external_selection, read_settings, write_blob)

SEGMENTS = [(0, 0x6000000)]

EXTERNAL = """<?xml version="1.0"?>
<Patch>
  <TitleID><ID>CUSA13795</ID></TitleID>
  <Metadata Title="CTR" Name="On" Author="x" PatchVer="1.0" AppVer="01.21" AppElf="eboot.bin" isEnabled="true">
    <PatchList><Line Type="bytes" Address="0x00500000" Value="9090"/></PatchList>
  </Metadata>
  <Metadata Title="CTR" Name="Off" Author="x" PatchVer="1.0" AppVer="01.21" AppElf="eboot.bin">
    <PatchList><Line Type="bytes32" Address="0x00500010" Value="0x11223344"/></PatchList>
  </Metadata>
  <Metadata Title="CTR" Name="Mask" Author="x" PatchVer="1.0" AppVer="01.21" AppElf="eboot.bin" isEnabled="true">
    <PatchList><Line Type="mask" Address="" Value="90 90"/></PatchList>
  </Metadata>
  <Metadata Title="CTR" Name="Old" Author="x" PatchVer="1.0" AppVer="01.00" AppElf="eboot.bin" isEnabled="true">
    <PatchList><Line Type="bytes" Address="0x00500020" Value="90"/></PatchList>
  </Metadata>
</Patch>
"""


class ExternalPatchTests(unittest.TestCase):
    def test_selection_filters_version_title_and_mask_patches(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            (folder / 'extra.xml').write_text(EXTERNAL)
            (folder / 'other.xml').write_text(EXTERNAL.replace('CUSA13795', 'CUSA99999'))
            found = external_patches(folder)
            self.assertEqual([key for key, _, _ in found], ['extra.xml/On', 'extra.xml/Off', 'extra.xml/Mask'])
            # isEnabled selects On and Mask; Mask is skipped whole for its unsupported line.
            writes = compile_external(external_selection(found, None), SEGMENTS)
            self.assertEqual(writes, [(0x100000, b'\x90\x90')])
            config = folder / 'patches.json'
            config.write_text(json.dumps({'enabled': ['extra.xml/Off'], 'disabled': ['extra.xml/On']}))
            writes = compile_external(external_selection(found, config), SEGMENTS)
            self.assertEqual(writes, [(0x100010, b'\x44\x33\x22\x11')])

    def test_out_of_segment_patch_is_skipped(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            (folder / 'extra.xml').write_text(EXTERNAL)
            found = external_patches(folder)
            self.assertEqual(compile_external(external_selection(found, None), [(0, 0x1000)]), [])

    def test_encode_types(self):
        import xml.etree.ElementTree as ET
        line = ET.fromstring('<Line Type="float32" Address="0x400000" Value="1.0"/>')
        self.assertEqual(encode(line), struct.pack('<f', 1.0))
        line = ET.fromstring('<Line Type="utf8" Address="0x400000" Value="ab"/>')
        self.assertEqual(encode(line), b'ab\0')
        with self.assertRaises(ValueError):
            encode(ET.fromstring('<Line Type="mask" Address="" Value="90"/>'))

    def test_eboot_segments_and_blob(self):
        elf = bytearray(120)
        struct.pack_into('<Q', elf, 0x20, 64)
        struct.pack_into('<HH', elf, 0x36, 56, 1)
        struct.pack_into('<IIQQQQQQ', elf, 64, 1, 0, 0, 0x1000, 0, 0, 0x2000, 0)
        self.assertEqual(eboot_segments(bytes(elf)), [(0x1000, 0x3000)])
        with tempfile.TemporaryDirectory() as tmp:
            write_blob(Path(tmp), [(0x10, b'\x90')])
            blob = (Path(tmp) / 'patches.bin').read_bytes()
            self.assertEqual(blob[:24], struct.pack('<8sQQ', b'BBPATCH2', EBOOT_BASE, 1))
            self.assertEqual(blob[24:], struct.pack('<QQ', 0x10, 1) + b'\x90')


class GamePatchTests(unittest.TestCase):
    """The bundled CTR patches, chosen through bbport.ini patch_* keys (the in-game menu)."""

    def test_bundled_file_lists_every_menu_patch(self):
        found = external_patches(ROOT / 'patches')
        keys = {key for key, _, _ in found}
        for patch in GAME_PATCH_KEYS.values():
            self.assertIn(patch, keys)
        self.assertEqual(external_patches(ROOT / 'patches', title_id='CUSA99999'), [])
        self.assertEqual(external_patches(ROOT / 'patches', app_version='01.00'), [])

    def test_ini_keys_select_patches(self):
        found = external_patches(ROOT / 'patches')
        self.assertEqual(external_selection(found, None, {}), [])
        selected = external_selection(found, None, {'patch_60fps': '1', 'patch_no_motion_blur': '0'})
        self.assertEqual([key for key, _, _ in selected], ['CrashTeamRacing.xml/60 FPS Unlock'])
        writes = compile_external(selected, SEGMENTS)
        self.assertEqual(len(writes), 1)
        self.assertEqual(writes[0][0], 0x017a9d90 - EBOOT_BASE)

    def test_read_settings(self):
        with tempfile.TemporaryDirectory() as tmp:
            ini = Path(tmp) / 'bbport.ini'
            ini.write_text('# comment\npatch_60fps=1\nfps_counter = 0\n')
            self.assertEqual(read_settings(ini), {'patch_60fps': '1', 'fps_counter': '0'})
            self.assertEqual(read_settings(Path(tmp) / 'missing.ini'), {})


if __name__ == '__main__':
    unittest.main()
