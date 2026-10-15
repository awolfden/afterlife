#include <cassert>
#include <cstring>
#include "../firmware/pet/main/audio-path-probe.h"
static uint8_t codec[256]{},ioe[256]{};
static unsigned masks=0,fail_mask_at=0,read_count=0,fail_read_at=0;
static bool fail_codec=false,bad_read=false;
static int64_t now=1000;
static int64_t clock_ms(){return now;}
static bool mask_reg(uint8_t address,uint8_t reg,uint8_t clear,uint8_t set){
 assert(address==0x4f);assert(!(reg==6&&set));assert(!((reg==4||reg==0x14)&&(set&1)));
 if(++masks==fail_mask_at)return false;
 ioe[reg]=(ioe[reg]&~clear)|set;
 if(reg==5){if(set==4)rail=true;else{assert(!clocked&&!tx_running&&routed==0);rail=false;}}
 return true;
}
static bool write_reg(uint8_t address,uint8_t reg,uint8_t value){assert(address==0x18&&rail&&!clocked);if(fail_codec)return false;codec[reg]=value;return true;}
static bool read_reg(uint8_t address,uint8_t reg,uint8_t& value){assert(rail);if(++read_count==fail_read_at)return false;value=address==0x18?codec[reg]:ioe[reg];if(bad_read&&address==0x18&&reg==0x32)value=0;return true;}
static void reset(){
 rail=clocked=fail_alloc=fail_init=fail_delete=fail_gpio=false;routed=allocations=0;events.clear();
 tx_running=preloaded=fail_preload=short_preload=fail_enable=fail_disable=fail_callback=false;tx_callback=nullptr;tx_context=nullptr;pcm_samples.clear();
 masks=fail_mask_at=read_count=fail_read_at=0;fail_codec=bad_read=false;now=1000;memset(codec,0,sizeof codec);memset(ioe,0,sizeof ioe);
}
int main(){
 reset();watcher_audio::PathProbe p(mask_reg,write_reg,read_reg,clock_ms);p.stop();p.tick(10000);assert(!masks);
 assert(p.init(1000));assert(!p.init(1000));assert(p.stage()==4&&p.valid()==65535&&!p.mismatch());
 for(auto sample:pcm_samples)assert(sample==0);assert(pcm_samples.size()==12288);assert(!(ioe[6]&2));
 dma_progress(34);p.tick(1199);assert(tx_running);p.tick(1200);
 assert(p.ready()&&!p.fault()&&p.stage()==6&&p.blocks()==34&&!rail&&!clocked&&!tx_running&&routed==0);assert(read_count==32);
 reset();watcher_audio::PathProbe no_dma(mask_reg,write_reg,read_reg,clock_ms);assert(no_dma.init(1000));no_dma.tick(1200);assert(!no_dma.ready()&&(no_dma.errors()&256)&&!rail);
 reset();bad_read=true;watcher_audio::PathProbe mismatch(mask_reg,write_reg,read_reg,clock_ms);assert(!mismatch.init(1000));assert(mismatch.mismatch()==256&&(mismatch.errors()&8)&&!clocked&&!rail);
 for(unsigned i=1;i<=16;i++){reset();fail_read_at=i;watcher_audio::PathProbe f(mask_reg,write_reg,read_reg,clock_ms);assert(!f.init(1000));assert(f.fault()&&!(f.valid()&(1u<<(i-1)))&&!rail);}
 for(unsigned i=1;i<=7;i++){reset();fail_mask_at=i;watcher_audio::PathProbe f(mask_reg,write_reg,read_reg,clock_ms);assert(!f.init(1000));assert(f.fault()&&!rail&&!clocked);}
 for(auto flag:{&fail_codec,&fail_alloc,&fail_init,&fail_callback,&fail_preload,&short_preload,&fail_enable}){
  reset();*flag=true;watcher_audio::PathProbe f(mask_reg,write_reg,read_reg,clock_ms);assert(!f.init(1000));assert(f.fault()&&!rail&&!clocked&&!tx_running);
 }
 for(auto flag:{&fail_disable,&fail_delete,&fail_gpio}){
  reset();watcher_audio::PathProbe f(mask_reg,write_reg,read_reg,clock_ms);assert(f.init(1000));*flag=true;f.stop();assert(f.fault()&&rail);*flag=false;f.deinit();assert(!rail&&!clocked&&!tx_running&&f.fault());
 }
 reset();watcher_audio::PathProbe cancelled(mask_reg,write_reg,read_reg,clock_ms);assert(cancelled.init(1000));cancelled.stop();assert(!cancelled.ready()&&!rail&&!clocked&&!cancelled.init(1000));
 reset();watcher_audio::PathProbe later_read(mask_reg,write_reg,read_reg,clock_ms);assert(later_read.init(1000));dma_progress(1);fail_read_at=17;later_read.tick(1200);assert(later_read.fault()&&!later_read.ready()&&!rail);
}
