#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace watcher_settings {
// Presentation only. Logical row selection and render generation live elsewhere.
class Scroll {
 float position_=0,origin_=0,from_=0,target_=0;int64_t start_=0,last_=-1;
 bool dragging_=false,settling_=false;
 public:
 void reset(unsigned top){position_=origin_=from_=target_=static_cast<float>(top*72);dragging_=settling_=false;last_=-1;}
 bool moving()const{return dragging_||settling_;}
 int update(int64_t now,unsigned top,bool drag,int displacement,bool animate){
  if(top>2||now<0){reset(0);return 0;}
  const float destination=static_cast<float>(top*72);
  if(!animate||(last_>=0&&now<last_)){reset(top);last_=now;return static_cast<int>(destination);}
  last_=now;
  if(settling_){const float t=std::min(1.0f,static_cast<float>(now-start_)/200.0f),remaining=1.0f-t;position_=from_+(target_-from_)*(1.0f-remaining*remaining*remaining*remaining);if(t>=1){position_=target_;settling_=false;}}
  if(drag){
   if(!dragging_){origin_=destination;dragging_=true;settling_=false;}
   position_=std::clamp(origin_-static_cast<float>(displacement),std::max(0.0f,origin_-72.0f),std::min(144.0f,origin_+72.0f));
  }else if(dragging_||(!settling_&&position_!=destination)||target_!=destination){
   dragging_=false;from_=position_;target_=destination;start_=now;settling_=position_!=target_;
  }
  return static_cast<int>(std::lround(position_));
 }
};
} // namespace watcher_settings
