#!/usr/bin/env python3
"""Identity-bound or explicit-port byte pipe. No reset/bootloader toggles or flash writes."""
import os
import select
import sys
import serial
from serial_transport import NoControlSerial
from device_identity import select_device
import re
if len(sys.argv) == 3 and sys.argv[1] == '--serial':
    from serial.tools import list_ports
    try:
        path = select_device(list_ports.comports(), sys.argv[2])
    except (ValueError, OSError):
        raise SystemExit('Identity-matched badge is unavailable or ambiguous')
elif len(sys.argv) == 2 and re.fullmatch(r'/dev/cu\.usbmodem[0-9A-Za-z]+', sys.argv[1]):
    path = sys.argv[1]
else:
    raise SystemExit('An explicit port or --serial identity is required')
port = NoControlSerial(port=None, baudrate=115200, timeout=0, write_timeout=0.5, exclusive=True)
port.port = path
try:
    port.open()
    while True:
        readable, _, _ = select.select([port.fileno(), sys.stdin.fileno()], [], [], 0.2)
        if sys.stdin.fileno() in readable:
            data = os.read(sys.stdin.fileno(), 2048)
            if not data:
                break
            port.write(data)
        if port.fileno() in readable:
            data = port.read(2048)
            if data:
                sys.stdout.buffer.write(data)
                sys.stdout.buffer.flush()
except (serial.SerialException, OSError) as error:
    print('Serial transport unavailable: ' + str(error), file=sys.stderr)
    raise SystemExit(1)
finally:
    port.close()
