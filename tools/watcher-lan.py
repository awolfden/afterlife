#!/usr/bin/env python3
"""Explicit foreground RAM-only Wi-Fi provisioning. No saved credentials or flashing."""
import argparse
import getpass
import ipaddress
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import warnings
import unicodedata


def validate_ssid(ssid):
    if not isinstance(ssid, str) or not 1 <= len(ssid.encode('utf-8')) <= 32 or any(unicodedata.category(c) in {'Cc', 'Cf', 'Cs', 'Zl', 'Zp'} for c in ssid):
        raise ValueError('Invalid Wi-Fi fields')


def credential_payload(ssid, password, address):
    ip = ipaddress.IPv4Address(address)
    if not any(ip in ipaddress.IPv4Network(n) for n in ['10.0.0.0/8', '172.16.0.0/12', '192.168.0.0/16']):
        raise ValueError('Private IPv4 address required')
    validate_ssid(ssid)
    if not 8 <= len(password) <= 63 or any(not 32 <= ord(c) <= 126 for c in password):
        raise ValueError('Invalid Wi-Fi fields')
    payload = bytearray(json.dumps({'ssid': ssid, 'password': password, 'address': str(ip), 'port': 0}, ensure_ascii=False).encode())
    if len(payload) > 1024:
        raise ValueError('Input too large')
    return payload


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address', required=True, help='This Mac\'s private LAN IPv4 address')
    usb = parser.add_mutually_exclusive_group(required=True)
    usb.add_argument('--port')
    usb.add_argument('--device-serial')
    parser.add_argument('--python', default=os.environ.get('WATCHER_PYTHON', sys.executable))
    parser.add_argument('--state-dir', default=str(Path.home()/'.local/share/pi-watcher-pet/run'))
    args = parser.parse_args()
    if not sys.stdin.isatty() or sys.platform != 'darwin':
        raise ValueError('Interactive macOS terminal required')
    warnings.simplefilter('error', getpass.GetPassWarning)
    # Validate the address before asking for secrets; no network discovery.
    credential_payload('validation', 'validation', args.address)
    if os.path.lexists(Path(args.state_dir)/'delivery.lock'):
        raise ValueError('Stop the existing host first')
    node = shutil.which('node')
    if not node:
        raise ValueError('Node unavailable')
    subprocess.run([args.python, '-c', 'import sys,serial; assert sys.platform=="darwin" and serial.VERSION=="3.5"'], check=True, timeout=10, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print('Keep USB connected for provisioning. Ctrl-C stops the host. No credentials are saved.')
    print('Passwords remain in process memory; Python/JavaScript strings cannot be guaranteed erased.')
    ssid = input('Wi-Fi network name (SSID): ')
    validate_ssid(ssid)
    password = getpass.getpass('Wi-Fi password (hidden): ')
    payload = credential_payload(ssid, password, args.address)
    command = [node, str(Path(__file__).with_suffix('.mjs')), '--python', args.python, '--state-dir', args.state_dir]
    command += ['--port', args.port] if args.port else ['--device-serial', args.device_serial]
    child = None
    try:
        child = subprocess.Popen(command, stdin=subprocess.PIPE)
        child.stdin.write(payload)
        child.stdin.close()
        payload[:] = b'\0' * len(payload)
        del password, ssid
        return child.wait()
    except KeyboardInterrupt:
        if child is not None:
            child.terminate()
            try:
                child.wait(timeout=10)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()
        return 130
    finally:
        payload[:] = b'\0' * len(payload)
        if child is not None:
            if child.stdin and not child.stdin.closed:
                try:
                    child.stdin.close()
                except OSError:
                    pass
            if child.poll() is None:
                child.terminate()
                try:
                    child.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait()


if __name__ == '__main__':
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
    except Exception:
        print('LAN setup failed. Check host ownership, selected Python/device, local IP and Wi-Fi input. No password is displayed.', file=sys.stderr)
        sys.exit(1)
