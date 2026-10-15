#pragma once
#include <cstdint>
namespace watcher_motor {
// No activation path. Establish/verify GPIO9 low BEFORE relinquishing PWM control.
// read(reg, value&) / write(reg, value) refer only to the already-identified IOE.
// Failed/ambiguous I2C transactions are not proof that a physical motor is off.
template<class Read,class Write> bool disable(Read read,Write write){
 auto checked=[&](uint8_t reg,uint8_t value){uint8_t actual=0;return write(reg,value)&&read(reg,actual)&&actual==value;};
 auto mask=[&](uint8_t reg,uint8_t clear,uint8_t set){uint8_t value=0;return read(reg,value)&&checked(reg,(value&~clear)|set);};
 if(!mask(0x06,1,0))return false; // Output latch low first; PWM still has priority.
 if(!mask(0x14,1,0))return false; // Push-pull.
 if(!mask(0x04,0,1))return false; // Output, already low when PWM relinquishes pin.
 if(!checked(0x1c,0))return false; // Disable PWM BEFORE changing its low-duty byte.
 if(!checked(0x1b,0))return false;
 uint8_t input=0;return read(0x08,input)&&!(input&1);
}
}
