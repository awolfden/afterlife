// Presentation only. No frame ACK, gesture, transport, content or voice authority.
#pragma once
#include <stdint.h>
namespace watcher_render {
struct FocusScene {
 uint32_t boot=0,generation=0,source=0,rotation=0,count=0,selected=0,roster=0,selection_epoch=0;
 uint16_t hue[4]{};
 uint8_t rhythm[4]{};
};
struct FocusPose {int x=0,y=0,scale=66;};
class FocusTransition {
 static constexpr int64_t duration=200;
 FocusScene scene_{};
 FocusPose from_[4]{},target_[4]{},shown_[4]{};
 int64_t began_=0,last_=0;
 bool initialized_=false,moving_=false,preview_=false;
 static unsigned visible(const FocusScene& scene){const unsigned n=scene.count-scene.selected/4*4;return n<4?n:4;}
 static bool valid(const FocusScene& scene){
  if(!scene.boot||scene.count<2||scene.count>16||scene.selected>=scene.count||scene.rotation>3||scene.source>2)return false;
  for(unsigned i=0;i<visible(scene);i++)if(scene.hue[i]>359||scene.rhythm[i]>3)return false;
  return true;
 }
 static FocusPose destination(const FocusScene& scene,unsigned slot){
  const unsigned selected=scene.selected%4;
  if(slot==selected)return {233,184,180};
  return {132+static_cast<int>(slot-(slot>selected?1:0))*101,302,66};
 }
 bool compatible(const FocusScene& next)const{
  if(scene_.boot!=next.boot||scene_.generation!=next.generation||scene_.source!=next.source||scene_.rotation!=next.rotation||scene_.count!=next.count||scene_.roster!=next.roster||scene_.selected/4!=next.selected/4)return false;
  for(unsigned i=0;i<visible(next);i++)if(scene_.hue[i]!=next.hue[i]||scene_.rhythm[i]!=next.rhythm[i])return false;
  return true;
 }
 static int mix(int from,int to,uint32_t weight){return from+static_cast<int>((static_cast<int64_t>(to-from)*weight)/65536);}
 void sample(int64_t now){
  if(!moving_)return;
  // Nonnegative timestamps; comparison before subtraction bounds all arithmetic.
  if(now-began_>=duration){for(unsigned i=0;i<4;i++)shown_[i]=target_[i];moving_=false;return;}
  const uint32_t remaining=65536-static_cast<uint32_t>((now-began_)*65536/duration);
  const uint32_t square=static_cast<uint32_t>((static_cast<uint64_t>(remaining)*remaining)>>16);
  const uint32_t weight=65536-static_cast<uint32_t>((static_cast<uint64_t>(square)*square)>>16); // ease-out quart
  for(unsigned i=0;i<4;i++)shown_[i]={mix(from_[i].x,target_[i].x,weight),mix(from_[i].y,target_[i].y,weight),mix(from_[i].scale,target_[i].scale,weight)};
 }
 public:
 void reset(){initialized_=false;moving_=preview_=false;}
 // release: 0=no gesture event, 1=awaiting host, 2=host event consumed.
 // Preview never mutates next.selected. Net distance, not time or velocity,
 // determines geometry: 6-unit dead zone, full pose at 100 touch units.
 bool update(const FocusScene& next,int64_t now,bool animate,bool dragging=false,int dx=0,unsigned release=0){
  if(now<0||!valid(next)){reset();return false;}
  const bool was_initialized=initialized_;
  if(!initialized_||now<last_||!compatible(next)||!animate||(scene_.selection_epoch!=next.selection_epoch&&release!=2)){
   scene_=next;for(unsigned i=0;i<4;i++)from_[i]=target_[i]=shown_[i]=i<visible(next)?destination(next,i):FocusPose{};
   began_=last_=now;initialized_=true;moving_=preview_=false;
   // A newly initialized scene may start tracking from its resting geometry.
   if(was_initialized||!dragging||!animate)return true;
  }
  if(dragging){
   // A new contact can catch a return/settle in flight without jumping back
   // to a resting pose. Keep this anchor fixed for the entire gesture.
   if(!preview_){sample(now);for(unsigned i=0;i<4;i++)from_[i]=shown_[i];}
   // Clamp before arithmetic, including hostile INT_MIN/INT_MAX inputs.
   const int distance=dx<-100?-100:dx>100?100:dx;
   const unsigned target=distance<0?(next.selected+1)%next.count:(next.selected+next.count-1)%next.count;
   FocusScene destination_scene=next;
   uint32_t weight=0;
   if(target/4==next.selected/4){
    destination_scene.selected=target;
    const int magnitude=distance<0?-distance:distance;
    if(magnitude>6)weight=static_cast<uint32_t>((magnitude-6)*65536/94);
   }
   for(unsigned i=0;i<visible(next);i++){
    const auto a=from_[i],b=destination(destination_scene,i);
    shown_[i]={mix(a.x,b.x,weight),mix(a.y,b.y,weight),mix(a.scale,b.scale,weight)};
   }
   scene_=next;last_=now;preview_=true;moving_=false;return true;
  }
  if(preview_){
   if(release==1){last_=now;return true;} // Hold only while an actual event is outstanding.
   for(unsigned i=0;i<4;i++){from_[i]=shown_[i];target_[i]=i<visible(next)?destination(next,i):FocusPose{};}
   began_=now;moving_=true;preview_=false;scene_=next;
  }
  sample(now);
  if(scene_.selected!=next.selected){
   for(unsigned i=0;i<4;i++){from_[i]=shown_[i];target_[i]=i<visible(next)?destination(next,i):FocusPose{};}
   began_=now;moving_=true;
  }
  scene_=next;last_=now;return true;
 }
 bool moving()const{return initialized_&&(moving_||preview_);}
 bool previewing()const{return initialized_&&preview_;}
 FocusPose pose(unsigned slot)const{return initialized_&&slot<visible(scene_)?shown_[slot]:FocusPose{};}
 // Small-to-large depth order; at most four current ghosts, never old-page copies.
 void order(unsigned (&slots)[4])const{
  for(unsigned i=0;i<4;i++)slots[i]=i;
  if(!initialized_)return;
  slots[0]=scene_.selected%4;unsigned next=1;
  for(unsigned i=0;i<visible(scene_);i++)if(i!=slots[0])slots[next++]=i;
  if(!moving_&&!preview_)return;
  for(unsigned i=1;i<visible(scene_);i++)for(unsigned j=i;j>0&&shown_[slots[j]].scale<shown_[slots[j-1]].scale;j--){const unsigned t=slots[j];slots[j]=slots[j-1];slots[j-1]=t;}
 }
};
} // namespace watcher_render
