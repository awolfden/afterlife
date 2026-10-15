#include "../firmware/pet/main/supervised-pulse.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <utility>
int main(){
 using namespace watcher_probe;
 Gesture g;for(int t=0;t<=3000;t+=10)assert(!g.update(3,t)); // Held at boot never arms.
 assert(!g.update(0,3010));assert(!g.update(0,3050));
 assert(!g.update(3,3100));assert(!g.update(3,5100));
 assert(!g.update(0,5110));assert(!g.update(0,5150));assert(g.phase==Gesture::Phase::armed);
 assert(!g.update(1,5200));assert(!g.update(1,5240));assert(!g.update(0,5300));assert(g.update(0,5340));
 for(int t=5350;t<15000;t+=10)assert(!g.update((t/100)%4,t));
 Gesture back;back.update(0,100);assert(!back.update(0,0));assert(back.phase==Gesture::Phase::consumed);
 for(int failure=0;failure<=11;failure++){
  Pulse p;int calls=0,off_count=0,delay_count=0,enables=0;
  auto read=[&](uint8_t,uint8_t& value){value=0;return ++calls!=failure;};
  auto write=[&](uint8_t reg,uint8_t value){if(reg==0x1c){assert(value==0x84||value==0);if(value)enables++;}return ++calls!=failure;};
  auto off=[&](){off_count++;return ++calls!=failure;};
  auto delay=[&](unsigned ms){assert(ms==80);delay_count++;};
  const bool ok=p.run(read,write,off,delay);(void)ok;
  const int prior=calls;assert(!p.run(read,write,off,delay));assert(calls==prior);
  assert(enables<=1&&delay_count<=1);if(enables)assert(off_count==2);
 }
 {
  Pulse p;std::vector<std::pair<int,int>> trace;
  assert(p.run([](uint8_t,uint8_t& v){v=0;return true;},[&](uint8_t r,uint8_t v){trace.emplace_back(r,v);return true;},[&](){trace.emplace_back(-1,0);return true;},[&](unsigned ms){trace.emplace_back(-2,ms);}));
  const std::vector<std::pair<int,int>> expected={{-1,0},{0x25,0xd0},{0x26,7},{0x1b,0},{0x1c,0x84},{-2,80},{0x1c,0},{-1,0}};
  assert(trace==expected);
 }
 {
  Pulse p;int off=0,clears=0;
  assert(!p.run([](uint8_t,uint8_t& v){v=0;return true;},[&](uint8_t r,uint8_t v){if(r==0x1c&&v==0x84)return false;if(r==0x1c&&v==0)clears++;return true;},[&](){off++;return true;},[](unsigned){assert(false);}));
  assert(off==2&&clears==1);
 }
 Pulse inherited;int writes=0;
 assert(!inherited.run([](uint8_t,uint8_t& v){v=0x80;return true;},[&](uint8_t,uint8_t){writes++;return true;},[]{return true;},[](unsigned){assert(false);}));
 assert(writes==0);
 puts("Supervised gesture, single-attempt, inherited-PWM and I/O-failure tests passed (software only)");
}
