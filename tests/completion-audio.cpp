#include <cassert>
#include <cstdlib>
#include "../firmware/pet/main/audio.h"
static uint8_t regs[256]{};
static bool fail_codec=false,fail_off=false;
static bool write_reg(uint8_t address,uint8_t reg,uint8_t value){
 assert(address==0x18);if(fail_codec)return false;
 if(reg==0x32)assert(value==0xbf);return true;
}
static bool mask_reg(uint8_t address,uint8_t reg,uint8_t clear,uint8_t set){
 assert(address==0x4f);
 // Motor pin8 must never be included in any mask.
 if(reg==0x06||reg==0x04||reg==0x14)assert(((clear|set)&1)==0);
 if(fail_off && reg==0x06 && clear==2)return false;
 regs[reg]=(regs[reg]&~clear)|set;return true;
}
static bool off(){return !(regs[0x06]&2)&&!(regs[0x05]&4);}
int main(){
 watcher_audio::Audio audio(write_reg,mask_reg);assert(audio.init());assert(off()&&!enabled);
 audio.start(0);assert(enabled&&!off());
 for(int at=0;at<220;at+=5)audio.tick(at,true);
 assert(samples.size()==5292*2);
 for(size_t i=0;i<samples.size();i+=2){assert(std::abs(samples[i])<=512);assert(samples[i]==samples[i+1]);}
 audio.tick(220,true);assert(off()&&!enabled&&audio.ready());
 audio.start(1000);audio.tick(1005,false);assert(off()&&!enabled);
 audio.start(2000);fail_write=true;audio.tick(2005,true);assert(off()&&!enabled&&!audio.ready());
 audio.start(3000);assert(off()&&!enabled);fail_write=false;
 watcher_audio::Audio codec_fail(write_reg,mask_reg);assert(codec_fail.init());fail_codec=true;
 codec_fail.start(0);assert(off()&&!enabled&&!codec_fail.ready());fail_codec=false;
 watcher_audio::Audio stop_fail(write_reg,mask_reg);assert(stop_fail.init());stop_fail.start(0);fail_off=true;stop_fail.stop();
 assert(!(regs[0x05]&4));assert(!enabled&&!stop_fail.ready());
}
