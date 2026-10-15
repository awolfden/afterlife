"""Read-only USB descriptor selection; never open/reset ports while discovering."""
import re


def normalize_serial(value):
    if not isinstance(value, str) or not re.fullmatch(r'(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}', value):
        raise ValueError('Expected an explicit ESP32 MAC-form USB serial number')
    return value.lower()


def select_device(ports, serial_number):
    expected = normalize_serial(serial_number)
    matches = []
    for port in ports:
        if port.vid != 0x303A or port.pid != 0x1001:
            continue
        try:
            observed = normalize_serial(port.serial_number)
        except ValueError:
            continue
        if observed == expected and re.fullmatch(r'/dev/cu\.usbmodem[0-9A-Za-z]+', port.device):
            matches.append(port.device)
    if len(matches) != 1:
        raise ValueError('Expected exactly one identity-matched badge')
    return matches[0]
