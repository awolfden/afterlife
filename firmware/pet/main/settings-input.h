#pragma once
#include "swipe.h"
#include <cstdint>
#include <cstdlib>
namespace watcher_settings {
struct Rect {int x,y,w,h;bool contains(int xx,int yy)const{return xx>=x&&yy>=y&&xx<x+w&&yy<y+h;}};
constexpr Rect entry_rect{165,406,136,40};
constexpr Rect scroll_rect{93,120,280,216};
constexpr Rect row_rect(unsigned visible){return {93,120+static_cast<int>(visible)*72,280,56};}
struct InputContext {bool enabled=false,open=false;uint32_t generation=0;unsigned rotation=0,top=0;};
enum class Action {none,open,back,toggle,scroll};
struct InputEvent {Action action=Action::none;unsigned row=0;int direction=0;bool claimed=false;};
class Input {
 bool armed_=false,down_=false,claimed_=false,moved_=false,bounced_=false,scrolled_=false;unsigned released_=0;
 int x_=0,y_=0,last_x_=0,last_y_=0,target_=-1;int64_t last_=-1,start_=0,release_=-1;
 InputContext origin_{};
 static int target(const InputContext& c,int x,int y){
  if(entry_rect.contains(x,y))return c.open?5:6;
  if(c.open)for(unsigned i=0;i<3;++i)if(row_rect(i).contains(x,y))return static_cast<int>(c.top+i);
  return -1;
 }
 static bool same(const InputContext& a,const InputContext& b){return a.enabled==b.enabled&&a.open==b.open&&a.generation==b.generation&&a.rotation==b.rotation&&a.top==b.top;}
 public:
 void cancel(){armed_=down_=claimed_=moved_=bounced_=scrolled_=false;released_=0;release_=-1;target_=-1;}
 bool contact()const{return down_;}
 bool dragging()const{return down_&&armed_&&!bounced_&&scrolled_;}
 int displacement()const{return last_y_-y_;}
 InputEvent update(watcher_touch::Sample sample,const InputContext& c,int64_t now){
  if(now<0||now<last_||!c.enabled||!c.generation||c.rotation>3||c.top>2||(down_&&(!same(c,origin_)||now-last_>100))){const bool claim=claimed_||c.open;cancel();last_=now;return {Action::none,0,0,claim};}
  last_=now;sample=watcher_touch::orient(sample,c.rotation);
  if(sample.kind==watcher_touch::Kind::error){const bool claim=claimed_||c.open;cancel();return {Action::none,0,0,claim};}
  if(sample.kind==watcher_touch::Kind::contact){
   const int x=sample.x*2>465?465:sample.x*2,y=sample.y*2>465?465:sample.y*2;
   if(!down_){down_=true;origin_=c;x_=last_x_=x;y_=last_y_=y;start_=now;release_=-1;target_=target(c,x,y);claimed_=c.open||target_==6;moved_=!armed_;}
   last_x_=x;last_y_=y;
   if(armed_&&c.open&&scroll_rect.contains(x_,y_)&&std::abs(y-y_)>16&&std::abs(y-y_)>2*std::abs(x-x_)){scrolled_=true;moved_=true;}
   if(std::abs(x-x_)>16||std::abs(y-y_)>16||now-start_>800)moved_=true;
   if(release_>=0){moved_=true;bounced_=true;} // Bounce invalidates taps AND scrolls.
   released_=0;release_=-1;return {Action::none,0,0,claimed_};
  }
  if(!down_){if(++released_>=2)armed_=true;return {Action::none,0,0,c.open};}
  const bool claim=claimed_;
  if(release_<0){release_=now;return {Action::none,0,0,claim};}
  if(now-release_<20)return {Action::none,0,0,claim};
  InputEvent result{Action::none,0,0,claim};
  if(claim&&armed_&&!bounced_&&now-start_<=1000){
   const int dx=last_x_-x_,dy=last_y_-y_;
   if(c.open&&scroll_rect.contains(x_,y_)&&std::abs(dy)>=32&&std::abs(dy)>std::abs(dx)*2)result={Action::scroll,0,dy<0?1:-1,true};
   else if(!moved_&&target_>=0&&target_==target(c,last_x_,last_y_)){
    if(target_==6)result.action=Action::open;
    else if(target_==5)result.action=Action::back;
    else {result.action=Action::toggle;result.row=static_cast<unsigned>(target_);}
   }
  }
  cancel(); // Require a clean release after every UI transition/gesture.
  return result;
 }
};
} // namespace watcher_settings
