#include <cassert>
#include <algorithm>
#include <cstdlib>
#include "../firmware/pet/main/audio-chime-probe.h"
static bool pa=false,fail_codec=false;
static unsigned masks=0,fail_mask_at=0,codec_writes=0;
static int64_t now_ms=1000;
static int64_t clock_ms(){return now_ms;}
static bool mask_reg(uint8_t address,uint8_t reg,uint8_t clear,uint8_t set){
 assert(address==0x4f);++masks;
 if(reg==0x06){assert((clear==2&&set==0)||(clear==0&&set==2));if(set==2)assert(rail&&clocked&&tx_running&&preloaded);}
 if(reg==0x04||reg==0x14)assert(!((clear|set)&1));
 if(masks==fail_mask_at)return false;
 if(reg==0x06){pa=set==2;events.push_back(pa?11:12);}
 if(reg==0x05){if(set==4){rail=true;events.push_back(1);}else{assert(!clocked&&!tx_running&&routed==0);rail=false;events.push_back(7);}}
 return true;
}
static bool write_reg(uint8_t address,uint8_t reg,uint8_t value){
 assert(address==0x18&&rail&&!clocked&&!pa);if(reg==0x32)assert(value==0xbf);++codec_writes;return !fail_codec;
}
static void reset(){rail=clocked=fail_alloc=fail_init=fail_delete=fail_gpio=pa=fail_codec=false;tx_running=preloaded=fail_preload=short_preload=fail_enable=fail_disable=false;routed=allocations=masks=fail_mask_at=codec_writes=0;events.clear();pcm_samples.clear();now_ms=1000;}
int main(){
 reset();watcher_audio::ChimeProbe p(mask_reg,write_reg,clock_ms);p.stop();p.tick(20000,true);assert(masks==0);
 assert(p.start());assert(pa&&rail&&clocked&&tx_running&&!p.ready()&&!p.fault());assert(codec_writes==8);assert(pcm_samples.size()==6144*2);
 int peak=0;
 for(size_t i=0;i<pcm_samples.size();i+=2){assert(pcm_samples[i]==pcm_samples[i+1]);const int level=std::abs(pcm_samples[i]);assert(level<=1024);peak=std::max(peak,level);if(i>=5292*2)assert(pcm_samples[i]==0);}
 assert(peak>1000);assert(!p.start());assert(allocations==1);
 p.tick(1179,true);assert(pa);p.tick(1180,true);assert(p.ready()&&!p.fault()&&!pa&&!rail&&!clocked&&!tx_running&&routed==0);
 assert(!p.start());p.deinit();assert(p.ready());
 reset();watcher_audio::ChimeProbe muted(mask_reg,write_reg,clock_ms);assert(muted.start());muted.tick(1001,false);assert(!pa&&!rail&&!clocked&&!muted.ready()&&!muted.fault());
 for(unsigned failure=1;failure<=8;failure++){
  reset();fail_mask_at=failure;watcher_audio::ChimeProbe f(mask_reg,write_reg,clock_ms);assert(!f.start());assert(f.fault()&&!pa&&!rail&&!clocked&&!tx_running);assert(!f.start());
 }
 for(auto flag:{&fail_alloc,&fail_init,&fail_codec,&fail_preload,&short_preload,&fail_enable}){
  reset();*flag=true;watcher_audio::ChimeProbe f(mask_reg,write_reg,clock_ms);assert(!f.start());assert(f.fault()&&!rail&&!clocked&&!tx_running&&!pa);
 }
 reset();watcher_audio::ChimeProbe f(mask_reg,write_reg,clock_ms);assert(f.start());fail_disable=true;f.stop();assert(f.fault()&&rail&&clocked&&!pa);fail_disable=false;f.deinit();assert(!rail&&!clocked&&!pa&&f.fault());
 reset();watcher_audio::ChimeProbe g(mask_reg,write_reg,clock_ms);assert(g.start());fail_gpio=true;g.stop();assert(g.fault()&&rail&&!clocked&&!pa);fail_gpio=false;g.deinit();assert(!rail&&g.fault());
}
