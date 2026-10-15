#define WATCHER_AUDIO_INIT_PROBE 1
#include <cassert>
#include <string>
#include <type_traits>
#include "../firmware/pet/main/audio.h"
#include "../firmware/pet/main/completion-chime.h"
static unsigned calls=0;
static bool write_reg(uint8_t,uint8_t,uint8_t){assert(false&&"silent probe must not initialize the codec");return false;}
static bool mask_reg(uint8_t address,uint8_t reg,uint8_t clear,uint8_t set){
 ++calls;assert(address==0x4f);
 if(reg==0x05){assert(clear==4);assert(set==0);} // Audio rail may only be disabled.
 if(reg==0x06){assert(clear==2);assert(set==0);} // PA may only be disabled.
 return true;
}
template<class T,class=void>struct has_start:std::false_type{};
template<class T>struct has_start<T,std::void_t<decltype(&T::start)>>:std::true_type{};
static std::string packet(char kind,unsigned boot,unsigned seq){auto p=std::string(1,kind)+"1 "+std::to_string(boot)+" "+std::to_string(seq)+" ";return p+std::to_string(watcher_wire::crc(p.data(),p.size()));}
int main(){
 static_assert(!has_start<watcher_audio::Audio>::value,"No playback method in silent image");
 watcher_audio::Audio audio(write_reg,mask_reg);assert(calls==0);audio.stop();audio.deinit();assert(calls==0);
 assert(!audio.ready());assert(audio.init());assert(calls==6);assert(!enabled&&samples.empty());
 assert(audio.init());assert(calls==6);audio.deinit();assert(!audio.ready());assert(!audio.init());
 watcher_chime::Gate gate('A');watcher_wire::Frame f{};f.boot=9;f.seq=1;f.count=1;
 auto take=[&](char kind,unsigned boot,unsigned seq,bool linked=true){auto p=packet(kind,boot,seq);return gate.take(p.data(),p.size(),f,1000,linked);};
 assert(!take('C',9,1));assert(!take('A',8,1));assert(!take('A',9,2));assert(!take('A',9,1,false));
 assert(take('A',9,1));assert(!take('A',9,1));
}
