// Original display-only paging model. Navigation remains exclusively in Swipe/host.
#pragma once
#include <cmath>
#include "carousel.h" // Shared scene/pose value types, not the old controller.
namespace watcher_render {
class MotionPager {
 CarouselScene identity_{};
 float position_=0,origin_=0,destination_=0,contact_origin_=0;
 int64_t last_=0,started_=0;
 bool ready_=false,contact_=false,settling_=false,pending_=false;
 static constexpr int64_t settle_ms=200;
 static float clamp(float n,float lo,float hi){return n<lo?lo:n>hi?hi:n;}
 static bool valid(const CarouselScene& s){return s.boot&&s.roster&&s.selection_epoch&&s.count>=2&&s.count<=16&&s.selected<s.count&&s.rotation<4&&s.source<=2;}
 bool same_ring(const CarouselScene& s)const{return s.boot==identity_.boot&&s.generation==identity_.generation&&s.source==identity_.source&&s.rotation==identity_.rotation&&s.count==identity_.count&&s.roster==identity_.roster;}
 void adopt(const CarouselScene& s,int64_t now){reset();identity_=s;ready_=true;last_=now;}
 void advance(int64_t now){
  if(!settling_)return;
  const float t=clamp(static_cast<float>(now-started_)/settle_ms,0,1),r=1-t;
  position_=origin_+(destination_-origin_)*(1-r*r*r*r);
  if(t==1){position_=destination_;settling_=false;}
 }
 void settle(float to,int64_t now){
  if(settling_&&destination_==to)return;
  origin_=position_;destination_=to;started_=now;settling_=position_!=to;
 }
 public:
 void reset(){ready_=contact_=settling_=pending_=false;position_=origin_=destination_=contact_origin_=0;last_=started_=0;}
 bool moving()const{return ready_&&(contact_||settling_||pending_);}
 bool previewing()const{return ready_&&(contact_||pending_||position_!=0);}
 float position()const{return position_;}
 // release: 0=no outstanding navigation, 1=await host, 2=host consumed it.
 bool update(const CarouselScene& next,int64_t now,bool animate,bool dragging=false,int dx=0,unsigned release=0,int direction=0){
  if(now<0||!valid(next)||release>2||direction < -1||direction>1){reset();return false;}
  if(!ready_||now<last_||!same_ring(next)){adopt(next,now);}
  if(!animate){adopt(next,now);return true;}
  advance(now);
  const bool changed=next.selected!=identity_.selected;
  if(changed||next.selection_epoch!=identity_.selection_epoch){
   int step=direction;
   const bool expected=release==2&&step!=0&&next.selected==(identity_.selected+next.count+step)%next.count;
   if(changed&&expected){
    // Preserve the same world position while moving the host-owned origin.
    position_-=step;origin_-=step;destination_-=step;contact_origin_-=step;
    settling_=false;pending_=false;contact_=false;
   }else{adopt(next,now);}
  }
  identity_=next;last_=now;
  if(dragging){
   if(!contact_)contact_origin_=position_;
   const int distance=dx < -100?-100:dx>100?100:dx;
   const int magnitude=distance<0?-distance:distance;
   const float travel=magnitude<=6?0:static_cast<float>(magnitude-6)/94;
   position_=clamp(contact_origin_+(distance<0?travel:-travel),-1,1);
   contact_=true;settling_=false;pending_=false;return true;
  }
  contact_=false;
  pending_=release==1;
  // Optimistic VISUAL settling is explicitly quarantined until host confirmation.
  // The authoritative selected index never changes here.
  settle(pending_&&direction?static_cast<float>(direction):0.0f,now);
  return true;
 }
 CarouselPose pose(unsigned index)const{
  if(!ready_||index>=identity_.count)return {};
  const float count=static_cast<float>(identity_.count);
  float d=static_cast<float>(index)-identity_.selected-position_;
  d-=std::floor((d+count/2)/count)*count;
  const float distance=std::fabs(d);
  if(identity_.count>4&&distance>=2)return {};
  constexpr float tau=6.283185307179586f;
  const float angle=d*tau/(identity_.count<4?count:4.0f);
  const float depth=std::cos(angle),front=depth>0?depth:0;
  const float scale=identity_.count==2?66+57*(depth+1):66+114*front*front;
  const unsigned alpha=identity_.count>4&&distance>1.5f?static_cast<unsigned>(clamp((2-distance)*510,0,255)):255;
  return {233+static_cast<int>(std::lround(std::sin(angle)*124)),240-static_cast<int>(std::lround(depth*56)),static_cast<int>(std::lround(scale)),static_cast<int>(std::lround(depth*32767)),alpha};
 }
 unsigned order(unsigned (&indices)[4])const{
  unsigned count=0;
  if(!ready_)return 0;
  for(unsigned i=0;i<identity_.count;++i)if(pose(i).alpha){if(count==4)return 0;indices[count++]=i;}
  for(unsigned i=1;i<count;++i)for(unsigned j=i;j&&pose(indices[j]).depth<pose(indices[j-1]).depth;--j){const auto t=indices[j];indices[j]=indices[j-1];indices[j-1]=t;}
  return count;
 }
};
}
