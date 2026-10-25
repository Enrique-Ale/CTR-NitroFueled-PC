from paths import ROOT
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('bbmods', ROOT / 'scripts/mods.py')
mods = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mods)


class ModTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.game = self.root / 'CUSA13795'
        self.assets = self.game / 'archives'
        self.assets.mkdir(parents=True)
        (self.game / 'eboot.bin').write_bytes(b'original executable')
        (self.game / 'sce_module').mkdir()
        (self.assets / 'a.pak').write_bytes(b'original')
        (self.assets / 'b.pak').write_bytes(b'untouched')
        self.moddir = self.root / 'mods'

    def mod(self, name, contents=b'mod', file='a.pak'):
        root = self.moddir / name
        folder = root / 'archives'
        folder.mkdir(parents=True, exist_ok=True)
        (folder / file).write_bytes(contents)
        return root

    def test_merge_replacement_new_file_and_directory_listing(self):
        a = self.mod('A')
        self.mod('A', b'new', 'new.pak')
        result = mods.build_overlay(self.game, self.root / 'out', [('A', a)])
        folder = result / 'archives'
        self.assertEqual((folder / 'a.pak').read_bytes(), b'mod')
        self.assertEqual((folder / 'b.pak').read_bytes(), b'untouched')
        self.assertEqual((folder / 'new.pak').read_bytes(), b'new')
        self.assertEqual({p.name for p in folder.iterdir()}, {'a.pak', 'b.pak', 'new.pak'})
        self.assertEqual((self.assets / 'a.pak').read_bytes(), b'original')
        self.assertFalse((self.assets / 'new.pak').exists())
        self.assertEqual((result / 'eboot.bin').read_bytes(), b'original executable')

    def test_last_mod_wins_and_reordering_changes_winner(self):
        a, b = self.mod('A', b'A'), self.mod('B', b'B')
        for layers, winner in [([('A', a), ('B', b)], b'B'), ([('B', b), ('A', a)], b'A')]:
            result = mods.build_overlay(self.game, self.root / 'out', layers)
            self.assertEqual((result / 'archives/a.pak').read_bytes(), winner)

    def test_selection_disable_and_new_mod(self):
        for name in ['C', 'B', 'A']:
            self.mod(name)
        config = self.root / 'mods.json'
        config.write_text(json.dumps({'order': ['B', 'A', 'deleted'], 'disabled': ['A']}))
        self.assertEqual(mods.selected(self.moddir, config), ['B', 'C'])

    def test_no_mod_returns_original_game(self):
        self.assertEqual(mods.build_overlay(self.game, self.root / 'out', []), self.game)
        self.assertFalse((self.root / 'out').exists())

    def test_directory_conflict_does_not_modify_base(self):
        a = self.mod('A')
        (a / 'archives/a.pak').unlink()
        nested = a / 'archives/b.pak'
        nested.mkdir()
        (nested / 'child').write_bytes(b'bad')
        with self.assertRaises(ValueError):
            mods.build_overlay(self.game, self.root / 'out', [('A', a)])
        self.assertEqual((self.assets / 'b.pak').read_bytes(), b'untouched')
        self.assertEqual(list((self.root / 'out').iterdir()), [])

    def test_mod_symlinks_rejected(self):
        a = self.mod('A')
        (a / 'archives/escape').symlink_to(self.assets, target_is_directory=True)
        with self.assertRaises(ValueError):
            mods.build_overlay(self.game, self.root / 'out', [('A', a)])

    def test_executable_replacement_rejected(self):
        a = self.mod('A')
        (a / 'eboot.bin').write_bytes(b'unsupported')
        with self.assertRaises(ValueError):
            mods.build_overlay(self.game, self.root / 'out', [('A', a)])

    def test_shadps4_sibling_folder_and_global_disable(self):
        legacy = Path(str(self.game) + '-mods')
        folder = legacy / 'archives'
        folder.mkdir(parents=True)
        (folder / 'a.pak').write_bytes(b'legacy')
        for enabled, expected in [('1', b'legacy'), ('0', b'original')]:
            result = subprocess.run([sys.executable, str(ROOT / 'scripts/mods.py'), str(self.game),
                '--out', str(self.root / 'out'), '--mods-dir', str(self.moddir), '--enabled', enabled],
                capture_output=True, text=True, check=True)
            self.assertEqual((Path(result.stdout.strip()) / 'archives/a.pak').read_bytes(), expected)

    def test_app0_wrapped_mod_discovered(self):
        self.moddir.mkdir()
        (self.moddir / 'Wrapped/app0/archives').mkdir(parents=True)
        self.assertEqual(mods.discover(self.moddir), ['Wrapped'])

    def test_other_case_replaces_the_game_file(self):
        root = self.moddir / 'Upper'
        (root / 'Archives').mkdir(parents=True)
        (root / 'Archives/A.PAK').write_bytes(b'upper')
        result = mods.build_overlay(self.game, self.root / 'out', [('Upper', root)])
        folder = result / 'archives'
        self.assertEqual((folder / 'a.pak').read_bytes(), b'upper')
        self.assertEqual({p.name for p in folder.iterdir()}, {'a.pak', 'b.pak'})
        self.assertEqual({p.name for p in result.iterdir()}, {'archives', 'eboot.bin', 'sce_module'})

    def test_wrapped_and_bare_layouts(self):
        layouts = {'Title': 'CUSA13795/archives', 'EU': 'CUSA14876/archives',
                   'Archive': 'Archive v1.2/archives', 'Bare': '', 'BareWrapped': 'BareWrapped v2',
                   'Nested': 'Nested/app0/archives'}
        for name, path in layouts.items():
            folder = self.moddir / name / path
            folder.mkdir(parents=True)
            (folder / 'a.pak').write_bytes(name.encode())
        (self.moddir / 'Archive/readme.txt').write_text('notes')
        (self.moddir / 'Junk/stuff').mkdir(parents=True)
        (self.moddir / 'BareWrapped/BareWrapped v2/preview.png').write_bytes(b'picture')
        (self.moddir / 'Bare/readme.txt').write_text('notes')
        self.assertEqual(mods.discover(self.moddir), ['Archive', 'Bare', 'BareWrapped', 'EU', 'Nested', 'Title'])
        for name in layouts:
            with self.subTest(name=name):
                result = mods.build_overlay(self.game, self.root / 'out',
                                            [(name, self.moddir / name)])
                self.assertEqual((result / 'archives/a.pak').read_bytes(), name.encode())

    def test_bare_archives_go_to_archives_and_extras_stay_out(self):
        root = self.moddir / 'Kart'
        (root / 'screenshots').mkdir(parents=True)
        (root / 'a.pak').write_bytes(b'bare')
        (root / 'readme.txt').write_text('notes')
        (root / 'screenshots/kart.pak').write_bytes(b'not an archive of the game')
        result = mods.build_overlay(self.game, self.root / 'out', [('Kart', root)])
        self.assertEqual((result / 'archives/a.pak').read_bytes(), b'bare')
        self.assertEqual({p.name for p in (result / 'archives').iterdir()}, {'a.pak', 'b.pak'})
        self.assertEqual({p.name for p in result.iterdir()}, {'archives', 'eboot.bin', 'sce_module'})

    def test_system_folder_replacement_rejected(self):
        a = self.mod('A')
        (a / 'sce_module').mkdir()
        (a / 'sce_module/libc.prx').write_bytes(b'unsupported')
        with self.assertRaises(ValueError):
            mods.build_overlay(self.game, self.root / 'out', [('A', a)])

    def test_other_game_folders_are_found(self):
        (self.game / 'movies').mkdir()
        (self.game / 'movies/intro.bk2').write_bytes(b'original')
        root = self.moddir / 'Intro'
        (root / 'Movies').mkdir(parents=True)
        (root / 'Movies/intro.bk2').write_bytes(b'new intro')
        self.assertEqual(mods.discover(self.moddir, mods.game_folders(self.game)), ['Intro'])
        self.assertNotIn('sce_module', mods.game_folders(self.game))
        result = mods.build_overlay(self.game, self.root / 'out', [('Intro', root)])
        self.assertEqual((result / 'movies/intro.bk2').read_bytes(), b'new intro')

    def test_bloodborne_layout_is_not_a_mod(self):
        (self.moddir / 'Old/dvdroot_ps4/chr').mkdir(parents=True)
        (self.moddir / 'Old/dvdroot_ps4/chr/c0000.chrbnd.dcx').write_bytes(b'bloodborne')
        self.assertEqual(mods.discover(self.moddir), [])

    def test_invalid_profile_fails(self):
        self.mod('A')
        config = self.root / 'mods.json'
        config.write_text('{"disabled":"A"}')
        with self.assertRaises(ValueError):
            mods.selected(self.moddir, config)

    def test_run_uses_overlay_propagates_exit_and_cleans_view(self):
        self.mod('A')
        python = self.root / 'python'
        python.write_text(f'#!{sys.executable}\nimport subprocess,sys\n'
            'if sys.argv[1] == "scripts/mods.py" or sys.argv[1] == "-c":\n'
            '    sys.exit(subprocess.call([sys.executable,*sys.argv[1:]]))\n')
        python.chmod(0o755)
        probe = self.root / 'probe'
        probe.write_text(f'#!{sys.executable}\nimport json,sys,os\nfrom pathlib import Path\n'
            'game=Path(sys.argv[sys.argv.index("--app0")+1])\n'
            'Path(os.environ["BB_DATA_DIR"],"mounted.json").write_text(json.dumps({\n'
            '"path":str(game),"content":(game/"archives/a.pak").read_text()}))\n'
            'sys.exit(7)\n')
        probe.chmod(0o755)
        env = dict(os.environ, BB_PREBUILT='1', BB_PROBE=str(probe), PYTHON=str(python),
            BB_DATA_DIR=str(self.root), BB_GAME_DIR=str(self.game),
            BB_MODS_DIR=str(self.moddir), BB_MODS_ENABLED='1', BB_MODS_CONFIG=str(self.root/'mods.json'))
        result = subprocess.run(['bash', 'run.sh'], cwd=ROOT, env=env, capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 7, result.stderr)
        mounted = json.loads((self.root / 'mounted.json').read_text())
        self.assertEqual(mounted['content'], 'mod')
        self.assertFalse(Path(mounted['path']).exists())
        self.assertEqual((self.assets/'a.pak').read_bytes(), b'original')
