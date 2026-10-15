#include "../firmware/pet/main/motor-off.h"
#include <array>
#include <cassert>
#include <cstdio>
struct Device {
 std::array<unsigned char,256> regs{};
 int operations=0,fail=-1;bool mismatched=false;
 bool read(unsigned char r,unsigned char& v){if(++operations==fail)return false;v=r==8?(regs[6]&1):regs[r];if(mismatched&&r==6)v|=1;return true;}
 bool write(unsigned char r,unsigned char v){if(++operations==fail)return false;
  // Neither changing direction nor relinquishing PWM may expose a high output latch.
  if((r==4&&(v&1))||r==0x1c)assert(!(regs[6]&1));
  assert(r!=0x1c || v==0);regs[r]=v;return true;
 }
 bool off(){return watcher_motor::disable([&](auto r,auto& v){return read(r,v);},[&](auto r,auto v){return write(r,v);});}
};
int main(){
 int scenarios=0;
 for(unsigned initial=0;initial<256;initial++){
  Device d;d.regs[6]=initial;d.regs[4]=0xa0;d.regs[0x14]=0xff;d.regs[0x1b]=0xff;d.regs[0x1c]=0xcf;
  assert(d.off());assert(d.regs[6]==(initial&0xfe));assert(d.regs[4]==0xa1);assert(d.regs[0x14]==0xfe);assert(!d.regs[0x1b]&&!d.regs[0x1c]);++scenarios;
 }
 Device sample;assert(sample.off());
 for(int i=1;i<=sample.operations;i++){Device d;d.fail=i;assert(!d.off());++scenarios;}
 Device mismatch;mismatch.mismatched=true;assert(!mismatch.off());assert(mismatch.operations==3);++scenarios;
 printf("%d motor-off ordering/failure scenarios passed (software only)\n",scenarios);
}
