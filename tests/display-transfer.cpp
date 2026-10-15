#include "../firmware/render/display-transfer.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#define CHECK(expression) do {if(!(expression)){std::cerr<<__LINE__<<": " #expression "\n";std::abort();}} while(false)
using namespace watcher_render;
struct Bus {
 mutable bool active=false;bool allow=true;unsigned reserve_calls=0,floor=0,complete_after=0;mutable unsigned polls=0,remaining=0;
 void submit(){active=true;remaining=complete_after;}
 bool busy()const{++polls;if(active&&remaining&&!--remaining)active=false;return active;}
 bool reserveDMABuffer(unsigned bytes){++reserve_calls;if(!allow)return false;floor=bytes;return true;}
};
struct Framebuffer {
 struct Config {unsigned offset_rotation=0;} cfg;
 Bus* bus=nullptr;bool automatic=true;unsigned starts=0,depth=16;
 Bus* getBus()const{return bus;}bool getAutoDisplay()const{return automatic;}
 unsigned getStartCount()const{return starts;}unsigned getWriteDepth()const{return depth;}
 const Config& config()const{return cfg;}
};
struct Display {
 Framebuffer* panel=nullptr;int rotation=0,w=468,h=468,depth=16;
 int32_t x=3,y=5,cw=40,ch=50;unsigned fills=0,rotations=0;
 Framebuffer* getPanel()const{return panel;}int width()const{return w;}int height()const{return h;}
 int getColorDepth()const{return depth;}int getRotation()const{return rotation;}
 void getClipRect(int32_t* a,int32_t* b,int32_t* c,int32_t* d){*a=x;*b=y;*c=cw;*d=ch;}
 void setClipRect(int32_t a,int32_t b,int32_t c,int32_t d){x=a;y=b;cw=c;ch=d;}
 void fillScreen(uint16_t){CHECK(panel->bus->floor==936);CHECK(x==0&&y==0&&cw==468&&ch==468);++fills;panel->bus->submit();}
 void setRotation(unsigned r){CHECK(!panel->bus->active);rotation=r;++rotations;}
};
struct Canvas {
 int w=466,h=466,depth=16;bool buffer=true,throw_push=false;unsigned pushes=0;
 int width()const{return w;}int height()const{return h;}int getColorDepth()const{return depth;}
 const void* getBuffer()const{return buffer?this:nullptr;}
 void pushSprite(Display* d,int x,int y){CHECK(x==0&&y==0);CHECK(d->panel->bus->floor==936);CHECK(!d->panel->bus->active);++pushes;d->panel->bus->submit();if(throw_push)throw std::runtime_error("partial");}
};
struct Clock {int64_t time=0;int64_t operator()()const{return time;}};
struct Fixture {
 Bus bus;Framebuffer fb;Display display;Canvas canvas;Clock clock;
 DisplayTransfer<Display,Canvas,Framebuffer,Bus,Clock> adapter;
 Fixture():adapter(display,canvas,bus,clock,16){fb.bus=&bus;display.panel=&fb;}
 void init(){CHECK(adapter.initialize(fb,0));CHECK(!adapter.ready()&&adapter.pending());CHECK(display.x==3&&display.y==5&&display.cw==40&&display.ch==50);const auto polls=bus.polls;CHECK(!adapter.finish_band()&&bus.polls==polls+128);bus.active=false;CHECK(adapter.finish_band()&&adapter.ready());}
};
int main(){
 for(unsigned rotation=0;rotation<4;++rotation){
  Fixture f;f.init();CHECK(f.adapter.apply_rotation(rotation));CHECK(f.bus.reserve_calls==1);
  TransferJob job;CHECK(job.begin(rotation,16));
  unsigned steps=0;
  while(!job.publishable()){
   CHECK(++steps<200);job.step(f.adapter);
   if(job.pending()){
    CHECK(!job.publishable());const auto changes=f.display.rotations;
    CHECK(!f.adapter.apply_rotation((rotation+1)%4));CHECK(f.display.rotations==changes);
    f.bus.active=false;
   }
  }
  CHECK(f.canvas.pushes==job.submitted_bands()&&job.completed_bands()==job.submitted_bands());CHECK(f.bus.reserve_calls==1);
 }
 // A short final DMA can finish in the same bounded service opportunity.
 Fixture quick;quick.init();quick.bus.complete_after=3;TransferJob short_dma;CHECK(short_dma.begin(0,16));const auto quick_polls=quick.bus.polls;const auto done=short_dma.step(quick.adapter);CHECK(done.submitted&&done.completed_band&&!short_dma.pending()&&quick.bus.polls-quick_polls<=5);
 // Failed preconditions cause no clear, copy or reservation retries.
 for(unsigned failure=0;failure<9;++failure){
  Fixture f;switch(failure){case 0:f.bus.allow=false;break;case 1:f.fb.automatic=false;break;case 2:f.fb.starts=1;break;case 3:f.display.w=467;break;case 4:f.canvas.depth=24;break;case 5:f.canvas.buffer=false;break;case 6:f.fb.cfg.offset_rotation=1;break;case 7:f.bus.active=true;break;default:f.fb.bus=nullptr;break;}
  CHECK(!f.adapter.initialize(f.fb,0));CHECK(f.adapter.faulted()&&!f.display.fills&&!f.canvas.pushes);CHECK(!f.adapter.initialize(f.fb,0));
 }
 Fixture geometry;geometry.init();CHECK(!geometry.adapter.submit_band({0,0,466,466,0,16}));CHECK(geometry.adapter.fault()==DisplayFault::geometry&&!geometry.canvas.pushes);
 Fixture timeout;timeout.init();TransferJob stalled;CHECK(stalled.begin(0,16));stalled.step(timeout.adapter);CHECK(stalled.pending());
 timeout.clock.time=50001;const auto polls=timeout.bus.polls;stalled.step(timeout.adapter);CHECK(timeout.bus.polls==polls+1&&timeout.adapter.fault()==DisplayFault::completion_timeout);
 timeout.bus.active=false;stalled.step(timeout.adapter);CHECK(stalled.pending()&&!stalled.publishable()&&!timeout.adapter.ready());CHECK(timeout.adapter.bus_idle()); // Idle is NOT recovery/publication permission.
 Fixture regression;regression.clock.time=100;regression.init();regression.clock.time=99;TransferJob backwards;CHECK(backwards.begin(0,16));backwards.step(regression.adapter);CHECK(regression.adapter.fault()==DisplayFault::clock&&!backwards.publishable());
 Fixture unsolicited;unsolicited.init();unsolicited.bus.active=true;
 CHECK(!unsolicited.adapter.pending()&&!unsolicited.adapter.finish_band()&&unsolicited.adapter.fault()==DisplayFault::ownership);
 unsolicited.bus.active=false;CHECK(!unsolicited.adapter.finish_band()&&!unsolicited.adapter.ready());
 Fixture foreign;foreign.init();TransferJob changed;CHECK(changed.begin(0,16));changed.step(foreign.adapter);foreign.fb.starts=1;foreign.bus.active=false;changed.step(foreign.adapter);CHECK(foreign.adapter.fault()==DisplayFault::ownership&&!changed.publishable());
 Fixture exception;exception.init();exception.canvas.throw_push=true;TransferJob thrown;CHECK(thrown.begin(0,16));bool caught=false;try{thrown.step(exception.adapter);}catch(const std::runtime_error&){caught=true;}
 CHECK(caught&&exception.adapter.fault()==DisplayFault::submission&&thrown.pending());CHECK(exception.display.x==3&&exception.display.y==5&&exception.display.cw==40&&exception.display.ch==50);
 exception.bus.active=false;thrown.step(exception.adapter);CHECK(!thrown.publishable()&&!exception.adapter.ready());
 std::cout<<"display adapter: four rotations, checked boot reservation, completion, geometry, ownership, clock, terminal faults pass\n";
}
