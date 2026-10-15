#include <cassert>
#include <algorithm>
#include <type_traits>
#include "../firmware/pet/main/audio-init-probe.h"
static unsigned masks=0,fail_mask_at=0;
static bool mask_reg(uint8_t address,uint8_t reg,uint8_t clear,uint8_t set){
 assert(address==0x4f);++masks;
 // PA can never be enabled. Motor pin8 cannot be touched.
 if(reg==0x06){assert(clear==2&&set==0);}
 if(reg==0x04||reg==0x14)assert(!((clear|set)&1));
 if(masks==fail_mask_at)return false;
 if(reg==0x05){
  assert((clear==4&&set==0)||(clear==0&&set==4));
  if(set==4){rail=true;events.push_back(1);}
  else{assert(!clocked&&routed==0);rail=false;events.push_back(7);}
 }
 return true;
}
static void reset(){rail=clocked=fail_alloc=fail_init=fail_delete=fail_gpio=false;routed=allocations=masks=fail_mask_at=0;events.clear();}
template<class T,class=void>struct has_start:std::false_type{};
template<class T>struct has_start<T,std::void_t<decltype(&T::start)>>:std::true_type{};
int main(){
 static_assert(!has_start<watcher_audio::InitProbe>::value);
 reset();watcher_audio::InitProbe p(mask_reg);p.stop();p.deinit();p.tick(20000);assert(masks==0&&allocations==0);
 assert(p.init(100));assert(p.ready()&&rail&&clocked&&allocations==1);assert(p.init(200));assert(allocations==1);
 auto on=std::find(events.begin(),events.end(),1),delay=std::find(on,events.end(),2),clk=std::find(delay,events.end(),4);assert(on<delay&&delay<clk);
 p.tick(15099);assert(p.ready());p.tick(15100);assert(!p.ready()&&!p.fault()&&!rail&&!clocked&&routed==0);
 assert(!p.init(20000));assert(allocations==1);
 for(unsigned failure=1;failure<=7;failure++){
  reset();fail_mask_at=failure;watcher_audio::InitProbe f(mask_reg);assert(!f.init(0));assert(!f.ready()&&!rail&&!clocked);assert(!f.init(0));
 }
 reset();fail_alloc=true;watcher_audio::InitProbe a(mask_reg);assert(!a.init(0));assert(!rail&&!clocked);
 reset();fail_init=true;watcher_audio::InitProbe b(mask_reg);assert(!b.init(0));assert(!rail&&!clocked&&routed==0);
 reset();watcher_audio::InitProbe c(mask_reg);assert(c.init(0));fail_delete=true;c.deinit();assert(c.fault()&&rail&&clocked&&!c.ready());fail_delete=false;c.deinit();assert(!c.fault()&&!rail&&!clocked);
 reset();watcher_audio::InitProbe d(mask_reg);assert(d.init(0));fail_gpio=true;d.stop();assert(d.fault()&&rail&&!clocked);fail_gpio=false;d.deinit();assert(!d.fault()&&!rail&&routed==0);
}
