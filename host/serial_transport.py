"""Pinned macOS transport: never explicitly change DTR/RTS modem-control lines.

pyserial 3.5 applies both line states during open(), even when set beforehand.
On this ESP32-S3 that produced USB_UART_CHIP_RESET and selected init(). Suppress
those two hooks, not flow control or normal reads/writes. OS-level open/close
behavior still needs hardware qualification; this is not a universal no-reset
promise. No discovery, firmware writes, reset commands or unsupported fallback.
"""
import sys
import serial


def transport_class(module=serial, platform=sys.platform):
    if platform != 'darwin' or module.VERSION != '3.5':
        raise RuntimeError('Serial transport requires reviewed macOS/pyserial 3.5')
    if not all(hasattr(module.Serial, name) for name in ('_update_dtr_state','_update_rts_state')):
        raise RuntimeError('Reviewed serial line-control hooks are unavailable')
    class NoControlSerial(module.Serial):
        def _update_dtr_state(self):
            pass
        def _update_rts_state(self):
            pass
    return NoControlSerial


NoControlSerial = transport_class()
