#!/usr/bin/env python3
"""Build only. Never opens serial, flashes, resets, installs or downloads dependencies."""
import argparse
import os
from pathlib import Path
import subprocess


def build_commands(root, build_dir, profile, touch=False, accept_software_cutoff=False):
    root, build_dir = Path(root).resolve(), Path(build_dir).resolve()
    if build_dir == root or build_dir.is_relative_to(root):
        raise ValueError('Use a build directory outside the source checkout')
    if profile not in {'usb', 'lan', 'afterlife'}:
        raise ValueError('Unknown firmware profile')
    afterlife = profile == 'afterlife'
    if afterlife and not accept_software_cutoff:
        raise ValueError('Afterlife haptics requires explicit --accept-software-cutoff')
    if accept_software_cutoff and not afterlife:
        raise ValueError('Software cutoff acceptance is only valid for Afterlife')
    lan = profile in {'lan', 'afterlife'}
    cmake = os.environ.get('WATCHER_CMAKE', 'cmake')
    defaults = str(root/'firmware/pet/sdkconfig.defaults')
    if lan:
        defaults += ';'+str(root/'firmware/pet/sdkconfig.lan.overlay')
    if afterlife:
        defaults += ';'+str(root/'firmware/pet/sdkconfig.afterlife.overlay')
    flags = {'WATCHER_LAN': lan, 'WATCHER_FACTORY_FEEDBACK': afterlife,
             'WATCHER_COMPLETION_CHIME': afterlife, 'WATCHER_AUTO_ROTATE': True, 'WATCHER_GHOST_PAGES': True,
             'WATCHER_TOUCH_SWIPE': touch or afterlife, 'WATCHER_HAPTICS': afterlife, 'WATCHER_HAPTICS_SOFTWARE_CUTOFF': afterlife,
             'WATCHER_FACTORY_ACCEPTANCE': False, 'WATCHER_FACTORY_HAPTIC_ACCEPTANCE': False,
             'WATCHER_AUDIO_INIT_PROBE': False, 'WATCHER_AUDIO_CHIME_PROBE': False,
             'WATCHER_AUDIO_PATH_PROBE': False, 'WATCHER_SUPERVISED_MOTOR_TEST': False,
             'WATCHER_TOUCH_MEASURED_SCALE': afterlife, 'WATCHER_TOUCH_DIAGNOSTICS': afterlife,
             'WATCHER_TOUCH_USB_DIAGNOSTICS': afterlife, 'WATCHER_TOUCH_TIMING_DIAGNOSTICS': afterlife,
             'WATCHER_TOUCH_RANGE_DIAGNOSTICS': False, 'WATCHER_BOUNDED_RENDER': afterlife,
             'WATCHER_RETAINED_PAGING': afterlife, 'WATCHER_MOTION_METRICS': afterlife,
             'WATCHER_GHOST_FAMILY': afterlife, 'WATCHER_AFTERLIFE': afterlife}
    configure = [cmake, '-S', str(root/'firmware/pet'), '-B', str(build_dir), '-DIDF_TARGET=esp32s3',
                 '-DSDKCONFIG='+str(build_dir/'sdkconfig'), '-DSDKCONFIG_DEFAULTS='+defaults]
    configure += ['-D'+key+'='+('ON' if value else 'OFF') for key, value in flags.items()]
    return configure, [cmake, '--build', str(build_dir)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--profile', choices=['usb', 'lan', 'afterlife'], default='usb')
    parser.add_argument('--touch', action='store_true', help='Experimental; not hardware-qualified')
    parser.add_argument('--accept-software-cutoff', action='store_true', help='Explicitly acknowledge software-only motor cutoff for the Afterlife build; not flash authorization')
    args = parser.parse_args()
    if not os.environ.get('IDF_PATH') or not os.environ.get('WATCHER_M5GFX_PATH'):
        raise ValueError('Export the pinned SDK and WATCHER_M5GFX_PATH first; no installation attempted')
    # A fresh directory avoids inheriting persistent SDK settings from a different
    # profile. Rebuild an existing profile with cmake --build its directory.
    root = Path(__file__).resolve().parents[1]
    configure, compile_command = build_commands(root, args.build_dir, args.profile, args.touch, args.accept_software_cutoff)
    args.build_dir.mkdir(mode=0o700)
    env = {**os.environ, 'IDF_COMPONENT_MANAGER': '0', 'IDF_CCACHE_ENABLE': '0'}
    subprocess.run(configure, env=env, check=True)
    subprocess.run(compile_command, env=env, check=True)
    print('Build complete. Images are NOT approved for flashing or public distribution.')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print('Build stopped: '+str(error))
        raise SystemExit(1)
