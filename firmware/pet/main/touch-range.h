#pragma once
#include "swipe.h"
#include <cstddef>
namespace watcher_touch {
struct RangeSnapshot {uint32_t id=0;unsigned state=0,samples=0,xMin=0,xMax=0,yMin=0,yMax=0,elapsed=0,windows=0;};
// RAM aggregates only: no point history, bus operations, normalization or gesture effects.
class RangeWindow {
 RangeSnapshot s_{};Context origin_{};int64_t last_=-1,armed_=0,first_=0;bool halted_=false;
 bool active()const{return s_.state==1||s_.state==2;}
 void cancel(unsigned state){const uint32_t id=s_.id;const unsigned windows=s_.windows;s_={};s_.id=id;s_.windows=windows;s_.state=state;}
 bool clock(int64_t now){if(halted_||now<0||now>INT64_MAX-120000||(last_>=0&&now<last_)){halted_=true;cancel(5);return false;}last_=now;return true;}
 static bool eligible(const Context& c){return c.enabled&&c.boot&&c.shown&&c.count>1&&c.selected<c.count&&c.rotation<4;}
 static bool same(const Context& a,const Context& b){return a.boot==b.boot&&a.selected==b.selected&&a.count==b.count&&a.rotation==b.rotation&&a.source==b.source&&a.generation==b.generation;}
 void finish(int64_t now){s_.state=3;s_.elapsed=now-first_>1000?1000:static_cast<unsigned>(now-first_);}
 public:
 const RangeSnapshot& snapshot()const{return s_;}
 void guard(const Context& c,int64_t now){
  if(!clock(now))return;
  if(s_.id&&(!eligible(c)||!same(c,origin_))){cancel(5);return;}
  if(s_.state==1&&now-armed_>=120000)cancel(4);
  else if(s_.state==2&&now-first_>=1000)finish(now);
 }
 bool arm(uint32_t id,const Context& c,int64_t now){
  guard(c,now);if(halted_||!id||!eligible(c)||active()||s_.windows>=10)return false;
  const auto windows=s_.windows+1;s_={};s_.id=id;s_.windows=windows;s_.state=1;origin_=c;armed_=now;first_=0;return true;
 }
 void observe(const uint8_t* b,size_t size,bool ok,int64_t now){
  if(!clock(now)||!active())return;
  if(s_.state==1&&now-armed_>=120000){cancel(4);return;}
  if(s_.state==2&&now-first_>=1000){finish(now);return;}
  if(!ok||!b||size!=6||b[0]>1){cancel(5);return;}
  if(b[0]==0){if(s_.state==2)finish(now);return;}
  const unsigned x=((b[1]&15u)<<8)|b[2],y=((b[3]&15u)<<8)|b[4];
  if(s_.state==1){s_.state=2;first_=now;s_.xMin=s_.xMax=x;s_.yMin=s_.yMax=y;}
  else{if(x<s_.xMin)s_.xMin=x;if(x>s_.xMax)s_.xMax=x;if(y<s_.yMin)s_.yMin=y;if(y>s_.yMax)s_.yMax=y;}
  ++s_.samples;s_.elapsed=static_cast<unsigned>(now-first_);
  if(s_.samples>=50)finish(now);
 }
};
} // namespace watcher_touch
