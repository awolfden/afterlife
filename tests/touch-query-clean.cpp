#include "../firmware/pet/main/touch-usb.h"
#include <cassert>
int main(){
 for(unsigned version=3;version<=4;version++){
  watcher_touch::UsbDiagnosticGate gate;char line[64];const int head=std::snprintf(line,sizeof line,"TQ%u 1 9 ",version);const int tail=std::snprintf(line+head,sizeof(line)-head,"%u",watcher_wire::crc(line,head));
  assert(!gate.request(line,head+tail,0,1,true));assert(!gate.take(0,1,true,true));
 }
}
