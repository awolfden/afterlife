#include "../firmware/pet/main/touch.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <type_traits>
#include <utility>
using namespace watcher_touch;
template<class T,class=void>struct HasStats:std::false_type{};
template<class T>struct HasStats<T,std::void_t<decltype(std::declval<T>().stats())>>:std::true_type{};
template<class T,class=void>struct HasEmitted:std::false_type{};
template<class T>struct HasEmitted<T,std::void_t<decltype(std::declval<T>().emitted())>>:std::true_type{};
#ifdef WATCHER_TOUCH_DIAGNOSTICS
static_assert(HasStats<Sampler>::value&&HasEmitted<Swipe>::value);
#else
static_assert(!HasStats<Sampler>::value&&!HasEmitted<Swipe>::value);
static_assert(sizeof(Sampler)<=24,"Clean sampler retains no counter or diagnostic-gap storage");
#endif
static uint64_t trace=1469598103934665603ULL;
static void mix(uint64_t v){trace=(trace^v)*1099511628211ULL;}
static Sample inverse(int x,int y,unsigned r){
 if(r==1)return {Kind::contact,233-y,x};
 if(r==2)return {Kind::contact,233-x,233-y};
 if(r==3)return {Kind::contact,y,233-x};
 return {Kind::contact,x,y};
}
int main(){
 unsigned events=0;
 for(unsigned rotation=0;rotation<4;rotation++)for(bool left:{false,true}){
  Swipe swipe;Context c{true,123,42,2,3,rotation,1,0};
  assert(!swipe.update({Kind::released,0,0},c,0).kind);assert(!swipe.update({Kind::released,0,0},c,20).kind);assert(swipe.armed());
  for(int i=0;i<3;i++)assert(!swipe.update(inverse(left?180-i*40:60+i*40,100,rotation),c,40+i*40).kind);
  assert(!swipe.update({Kind::released,0,0},c,160).kind);const auto e=swipe.update({Kind::released,0,0},c,180);
  assert(e.kind==(left?2u:1u)&&e.boot==123&&e.shown==42);events++;mix(e.kind);mix(e.boot);mix(e.shown);
  assert(!swipe.update(inverse(100,100,rotation),c,300).kind&&!swipe.armed()); // >100ms cancels.
  c.enabled=false;assert(!swipe.guard(c)&&!swipe.armed()&&!swipe.tracking());
 }
 unsigned observations=0;
 for(unsigned failedStage=0;failedStage<5;failedStage++){
  Sampler sampler;Swipe swipe;Context c{true,123,42,2,3,0,1,0};bool failed=false;unsigned reads=0,writes=0;
  const auto fail=[&](unsigned stage){if(stage==failedStage&&!failed){failed=true;return true;}return false;};
  const auto read=[&](uint8_t reg,uint8_t* data,unsigned size){
   mix(reg);mix(size);++reads;
   if(reg==0xa7){assert(size==3);std::memset(data,0,size);return !fail(1);}
   assert(reg==0x02&&size==6);if(reads%37==0)return false;
   const unsigned x=(reads*37)%500,y=(reads*61)%500;const uint8_t packet[]={uint8_t(reads%17==0?2:reads%5<2?0:1),uint8_t(x>>8),uint8_t(x),uint8_t(y>>8),uint8_t(y),0};std::memcpy(data,packet,size);return true;
  };
  const auto write=[&](uint8_t reg,uint8_t value){++writes;mix(reg);mix(value);return !fail(reg==0?0:reg==0xfa?2:3);};
  for(int64_t now=0;now<=120000;now+=20){
   Sample sample;const bool polled=sampler.poll(now,sample,read,write);mix(polled);mix(sampler.stage());mix(sampler.ready());mix(sampler.halted());
   if(polled){mix(static_cast<unsigned>(sample.kind));mix(sample.x);mix(sample.y);const auto e=swipe.update(sample,c,now);mix(e.kind);mix(e.boot);mix(e.shown);mix(swipe.armed());mix(swipe.tracking());++observations;}
  }
  assert(sampler.ready()&&!sampler.halted());Sample sample;assert(sampler.poll(119000,sample,read,write)&&sampler.halted()&&sample.kind==Kind::error);assert(!sampler.poll(121000,sample,read,write));mix(reads);mix(writes);mix(observations);
 }
 assert(events==8&&observations>1000);
 std::cout<<"{\"trace\":\""<<std::hex<<trace<<std::dec<<"\",\"events\":"<<events<<",\"observations\":"<<observations<<",\"samplerBytes\":"<<sizeof(Sampler)<<",\"diagnosticApi\":"<<(HasStats<Sampler>::value?"true":"false")<<"}\n";
}
