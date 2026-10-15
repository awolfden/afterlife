#pragma once
#include <cstdint>
#include <initializer_list>
namespace watcher_probe {
// Experimental local gesture only. No USB command can arm/fire this test.
class Gesture {
 public:
 enum class Phase { release, hold, release_to_arm, armed, pressed, consumed };
 Phase phase=Phase::release;
 bool update(unsigned raw,int64_t now){
  if(now<last_time){phase=Phase::consumed;return false;}last_time=now;
  if(raw!=last_raw){last_raw=raw;changed=now;}
  if(phase==Phase::consumed||now-changed<40)return false;
  if((phase==Phase::armed||phase==Phase::pressed)&&now-armed_at>10000){phase=Phase::hold;return false;}
  switch(phase){
   case Phase::release:if(raw==0)phase=Phase::hold;break;
   case Phase::hold:if(raw==3&&now-changed>=2000)phase=Phase::release_to_arm;break;
   case Phase::release_to_arm:if(raw==0){phase=Phase::armed;armed_at=now;}break;
   case Phase::armed:if(raw==1){phase=Phase::pressed;pressed_at=changed;}else if(raw!=0)phase=Phase::hold;break;
   case Phase::pressed:
    if(now-pressed_at>1000||raw==2||raw==3){phase=Phase::hold;break;}
    if(raw==0){phase=Phase::consumed;return true;}break;
   case Phase::consumed:break;
  }
  return false;
 }
 private:unsigned last_raw=0;int64_t changed=0,last_time=0,armed_at=0,pressed_at=0;
};
// Single attempt per boot, including failures. This is NOT an independent cutoff.
// Caller must establish operator presence/physical stop before using the test image.
class Pulse {
 bool used=false;
 public:
 template<class Read,class Write,class Off,class Delay>
 bool run(Read read,Write write,Off off,Delay delay){
  if(used)return false;
  used=true;
  if(!off())return false;
  uint8_t value=0;
  // Do not disturb another inherited PWM channel with the shared frequency.
  for(uint8_t reg:{uint8_t(0x1e),uint8_t(0x20),uint8_t(0x22)})if(!read(reg,value)||(value&0x80))return false;
  if(!write(0x25,0xd0)||!write(0x26,0x07)||!write(0x1b,0)){off();return false;}
  const bool enabled=write(0x1c,0x84); // Active-high, ~25% duty, 2 kHz.
  if(enabled)delay(80);
  // Pre-check established the low latch/direction. Clear enable immediately;
  // an unrelated GPIO read failure must not prevent this first stop attempt.
  const bool disabled=write(0x1c,0);
  const bool stopped=off(); // Verify/attempt full off even after either ambiguous write.
  return enabled&&disabled&&stopped;
 }
};
}
