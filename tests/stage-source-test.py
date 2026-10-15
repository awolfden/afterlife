import hashlib
import importlib.util
import json
import os
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('stager', Path(__file__).resolve().parents[1]/'tools/stage-source.py')
stager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stager)


class SourceStageTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.root = self.base/'project'
        self.root.mkdir()
        (self.root/'release').mkdir()
        self.files = {'README.md': b'Synthetic preview\n', 'LICENSE': b'Synthetic license fixture\n', 'THIRD_PARTY_NOTICES.md': b'Synthetic notices\n'}
        for name, data in self.files.items():
            (self.root/name).write_bytes(data)
        self.manifest = {'version': '0.1.0-dev.1', 'publicationApproved': False, 'files': [{'path': name, 'sha256': hashlib.sha256(data).hexdigest(), 'license': 'MIT'} for name, data in self.files.items()]}
        self.save()

    def save(self):
        (self.root/stager.MANIFEST).write_text(json.dumps(self.manifest))

    def reject(self):
        with self.assertRaises((ValueError, OSError, UnicodeError)):
            stager.stage(self.root, self.base/'output')
        self.assertFalse((self.base/'output').exists())

    def test_deterministic_source_only_archive(self):
        a = stager.stage(self.root, self.base/'a')
        b = stager.stage(self.root, self.base/'b')
        self.assertEqual(a['sha256'], b['sha256'])
        self.assertFalse(a['publicationApproved'])
        with tarfile.open(self.base/'a'/a['archive']) as archive:
            names = archive.getnames()
            self.assertEqual(len(names), len(self.files)+3)
            self.assertTrue(all(m.isfile() and m.mtime == 0 and m.mode == 0o644 for m in archive.getmembers()))
            self.assertFalse(any(name.endswith('.bin') for name in names))

    def test_changed_source(self):
        (self.root/'README.md').write_bytes(b'Changed after manifest')
        self.reject()

    def test_missing_notice(self):
        self.manifest['files'] = [e for e in self.manifest['files'] if e['path'] != 'LICENSE']
        self.save(); self.reject()

    def test_unknown_license(self):
        self.manifest['files'][0]['license'] = 'unknown'
        self.save(); self.reject()

    def test_path_traversal(self):
        for path in ['../README.md', '/README.md', 'docs/../README.md', 'docs//README.md', './README.md']:
            with self.assertRaises(ValueError):
                stager.relative_path(path)

    def test_duplicate_case_collision(self):
        self.manifest['files'].append({**self.manifest['files'][0], 'path': 'readme.md'})
        self.save(); self.reject()

    def test_reserved_case_collision(self):
        self.manifest['files'].append({'path': 'Release/Inventory.json', 'sha256': '0'*64, 'license': 'MIT'})
        self.save(); self.reject()

    def test_version_and_test_closure(self):
        for name, data in [('package.json', b'{"version":"0.0.0-dev.1"}'), ('release/tests.json', b'{"node":["tests/missing.test.mjs"],"python":[]}')]:
            (self.root/name).write_bytes(data)
            self.manifest['files'].append({'path': name, 'sha256': hashlib.sha256(data).hexdigest(), 'license': 'MIT'})
            self.save(); self.reject()
            self.manifest['files'].pop()
            (self.root/name).unlink()

    def test_symlink_file(self):
        (self.root/'README.md').unlink()
        (self.base/'outside.md').write_bytes(self.files['README.md'])
        (self.root/'README.md').symlink_to(self.base/'outside.md')
        self.reject()

    def test_symlink_directory(self):
        (self.root/'outside').symlink_to(self.base, target_is_directory=True)
        with self.assertRaises(OSError):
            stager.read_source(self.root, 'outside/project/README.md')

    def test_hardlink(self):
        os.link(self.root/'README.md', self.base/'second-link.md')
        self.reject()

    def test_privacy_patterns(self):
        fixtures = [('/'+'Users'+'/someone/file').encode(), ('/'+'var'+'/folders/synthetic/T/example').encode(), ('/'+'private'+'/var'+'/folders/synthetic/T/example').encode(), ('-----BEGIN '+'PRIVATE KEY-----').encode(), ('ghp_'+'x'*24).encode(), ':'.join(['12','34','56','78','9a','bc']).encode(), b'zero\0byte']
        for raw in fixtures:
            with self.assertRaises(ValueError):
                stager.privacy_check(raw)

    def test_existing_destination_and_inside_tree(self):
        (self.base/'output').mkdir()
        with self.assertRaises(FileExistsError):
            stager.stage(self.root, self.base/'output')
        with self.assertRaises(ValueError):
            stager.stage(self.root, self.root/'output')
        self.assertFalse((self.root/'output').exists())

    def test_no_publication_flag_or_binary(self):
        self.manifest['publicationApproved'] = True
        self.save(); self.reject()
        with self.assertRaises(ValueError):
            stager.relative_path('firmware.bin')


if __name__ == '__main__':
    unittest.main()
