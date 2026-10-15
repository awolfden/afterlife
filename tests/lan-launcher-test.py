import importlib.util
from pathlib import Path
import json
import sys
import unittest
from unittest.mock import patch
import tempfile
import subprocess
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('launcher', Path(__file__).resolve().parents[1]/'tools/watcher-lan.py')
launcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(launcher)

class LanLauncherTest(unittest.TestCase):
    def test_ram_payload(self):
        payload = launcher.credential_payload('synthetic-network', 'synthetic-passphrase', '192.168.1.2')
        self.assertIsInstance(payload, bytearray)
        self.assertEqual(set(json.loads(payload)), {'ssid', 'password', 'address', 'port'})
        self.assertLessEqual(len(payload), 1024)

    def test_ssid_controls_rejected(self):
        for ssid in ['has\tcontrol', 'has\u200bformat', 'line\u2028separator', 'para\u2029separator', 'surrogate\ud800']:
            with self.assertRaises(ValueError):
                launcher.validate_ssid(ssid)

    def test_main_refuses_before_credentials(self):
        with tempfile.TemporaryDirectory() as directory:
            args = ['watcher-lan.py', '--address', '192.168.1.2', '--port', '/dev/cu.usbmodemTEST', '--state-dir', directory]
            for failure in ['tty', 'lock', 'node', 'voice', 'python']:
                with self.subTest(failure=failure):
                    argv = args + (['--voice-helper', '/synthetic/helper'] if failure == 'voice' else [])
                    with patch.object(launcher.sys, 'argv', argv), patch.object(launcher.sys, 'platform', 'darwin'), patch.object(launcher.sys.stdin, 'isatty', return_value=failure != 'tty'), patch.object(launcher.os.path, 'lexists', return_value=failure == 'lock'), patch.object(launcher.shutil, 'which', return_value=None if failure == 'node' else '/synthetic/node'), patch.object(launcher.subprocess, 'run', side_effect=subprocess.CalledProcessError(1, [])) as run, patch('builtins.input', side_effect=AssertionError('Unexpected credential prompt')), patch.object(launcher.getpass, 'getpass', side_effect=AssertionError('Unexpected password prompt')):
                        with self.assertRaises(SystemExit if failure == 'voice' else (ValueError, subprocess.CalledProcessError)):
                            launcher.main()
                        self.assertEqual(run.call_count, 1 if failure == 'python' else 0)

    def test_reject_fields(self):
        for ssid, password, address in [('', 'synthetic-passphrase', '192.168.1.2'), ('x'*33, 'synthetic-passphrase', '192.168.1.2'), ('test', 'short', '192.168.1.2'), ('test', 'secret\ninvalid', '192.168.1.2'), ('test', 'synthetic-passphrase', '8.8.8.8'), ('test', 'synthetic-passphrase', '127.0.0.1'), ('test', 'synthetic-passphrase', '::1')]:
            with self.assertRaises(ValueError):
                launcher.credential_payload(ssid, password, address)

if __name__ == '__main__':
    unittest.main()
