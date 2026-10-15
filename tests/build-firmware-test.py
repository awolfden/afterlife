import importlib.util
import os
from pathlib import Path
import sys
import tempfile
import unittest
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('builder', Path(__file__).resolve().parents[1]/'tools/build-firmware.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class BuildCommandsTest(unittest.TestCase):
    def test_profiles_are_build_only_and_explicit(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)/'root'
            for profile in ['usb', 'lan']:
                configure, compile_command = builder.build_commands(root, Path(temporary)/profile, profile)
                self.assertEqual(compile_command[1], '--build')
                self.assertIn('-DWATCHER_TOUCH_SWIPE=OFF', configure)
                self.assertIn('-DWATCHER_HAPTICS=OFF', configure)
                self.assertIn('-DWATCHER_SUPERVISED_MOTOR_TEST=OFF', configure)
                self.assertFalse(any('WATCHER_MIC_CAPTURE' in arg for arg in configure))
                self.assertEqual(any('sdkconfig.lan.overlay' in arg for arg in configure), profile == 'lan')
                self.assertFalse(any(arg in ['flash', 'erase', 'install'] for arg in configure+compile_command))
                self.assertIn('-DSDKCONFIG='+str((Path(temporary)/profile/'sdkconfig').resolve()), configure)
            configure, _ = builder.build_commands(root, Path(temporary)/'touch', 'usb', True)
            self.assertIn('-DWATCHER_TOUCH_SWIPE=ON', configure)
            with self.assertRaises(ValueError):
                builder.build_commands(root, root/'build', 'usb')
            with self.assertRaises(ValueError):
                builder.build_commands(root, Path(temporary)/'bad', 'unknown')

    def test_afterlife_requires_explicit_cutoff_and_preserves_complete_flag_set(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)/'root'
            build = Path(temporary)/'build'
            for profile in ['voice-usb', 'voice-lan', 'afterlife']:
                with self.assertRaises(ValueError):
                    builder.build_commands(root, build, profile)
            with self.assertRaises(ValueError):
                builder.build_commands(root, build, 'usb', accept_software_cutoff=True)
            configure, compile_command = builder.build_commands(root, build, 'afterlife', accept_software_cutoff=True)
            defaults = next(arg for arg in configure if arg.startswith('-DSDKCONFIG_DEFAULTS='))
            self.assertTrue(defaults.endswith('sdkconfig.lan.overlay;'+str(root.resolve()/'firmware/pet/sdkconfig.afterlife.overlay')))
            enabled = {'LAN','FACTORY_FEEDBACK','COMPLETION_CHIME','AUTO_ROTATE','GHOST_PAGES','TOUCH_SWIPE','HAPTICS','HAPTICS_SOFTWARE_CUTOFF','TOUCH_MEASURED_SCALE','TOUCH_DIAGNOSTICS','TOUCH_USB_DIAGNOSTICS','TOUCH_TIMING_DIAGNOSTICS','BOUNDED_RENDER','RETAINED_PAGING','MOTION_METRICS','GHOST_FAMILY','AFTERLIFE'}
            disabled = {'FACTORY_ACCEPTANCE','FACTORY_HAPTIC_ACCEPTANCE','AUDIO_INIT_PROBE','AUDIO_CHIME_PROBE','AUDIO_PATH_PROBE','SUPERVISED_MOTOR_TEST','TOUCH_RANGE_DIAGNOSTICS'}
            expected = {'-DWATCHER_'+key+'=ON' for key in enabled} | {'-DWATCHER_'+key+'=OFF' for key in disabled}
            self.assertEqual({arg for arg in configure if arg.startswith('-DWATCHER_')}, expected)
            self.assertFalse(any(arg in ['flash','erase','install'] for arg in configure+compile_command))


if __name__ == '__main__':
    unittest.main()
