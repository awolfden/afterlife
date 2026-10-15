"""Mock-only line-control contract tests; no port is opened."""
import sys,unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'host'))
class Parent:
 def __init__(self,**kwargs):
  self.kwargs=kwargs;self.controls=[];self._update_dtr_state();self._update_rts_state()
 def _update_dtr_state(self):self.controls.append('dtr')
 def _update_rts_state(self):self.controls.append('rts')
 def read(self,n):return bytes(n)
 def write(self,data):return len(data)
# Import-time platform/version checks run against a synthetic module as well:
# the portable suite never needs to install pyserial or open a real port.
with patch.dict(sys.modules, {'serial': SimpleNamespace(VERSION='3.5',Serial=Parent)}), patch.object(sys,'platform','darwin'):
 from serial_transport import transport_class
class Tests(unittest.TestCase):
 def test_no_modem_updates(self):
  cls=transport_class(SimpleNamespace(VERSION='3.5',Serial=Parent),'darwin')
  p=cls(port=None,timeout=0,exclusive=True)
  self.assertEqual(p.controls,[]);self.assertEqual(p.kwargs,{'port':None,'timeout':0,'exclusive':True})
  self.assertEqual(p.read(2),b'\0\0');self.assertEqual(p.write(b'x'),1)
 def test_unsupported_version(self):
  with self.assertRaises(RuntimeError):transport_class(SimpleNamespace(VERSION='4',Serial=Parent),'darwin')
 def test_unsupported_platform(self):
  with self.assertRaises(RuntimeError):transport_class(SimpleNamespace(VERSION='3.5',Serial=Parent),'linux')
 def test_missing_hook(self):
  with self.assertRaises(RuntimeError):transport_class(SimpleNamespace(VERSION='3.5',Serial=object),'darwin')
if __name__=='__main__':unittest.main()
