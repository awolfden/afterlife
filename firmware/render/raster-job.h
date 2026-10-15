// Resumable counterpart of ghost.h. No driver, clock, heap or authority effects.
#pragma once
#include "ghost.h"
namespace watcher_render {
static_assert(watcher_art::width<=logical_width&&watcher_art::height<=logical_height,"Personality source must fit legacy pre-scale clip");
enum class RasterPhase { idle, validating, drawing, complete, failed };
struct RasterProgress { unsigned work=0,spans=0; RasterPhase phase=RasterPhase::idle; };
// The descriptor is copied; its run storage must remain immutable until completion.
// Each work unit visits ONE run, including transparent/clipped/shrunk-away runs.
// Full validation precedes every emitted span. Callers bound wall time separately:
// a work limit does not certify the duration of the supplied canvas sink.
class RasterJob {
 watcher_art::Sprite sprite_{};
 RasterPhase phase_=RasterPhase::idle;
 size_t index_=0;
 unsigned position_=0;
 int cx_=0,cy_=0,xscale_=0,yscale_=0,row_=-1,top_=0,bottom_=0,lean_=0;
 unsigned hue_=0,archetype_=0;
 bool personality_=false;
 uint16_t colors_[64]{},tinted_[64]{};
 bool occupied_[64]{};
 static constexpr unsigned area=watcher_art::width*watcher_art::height;
 bool begin(const watcher_art::Sprite& sprite,int x,int y){
  *this=RasterJob{};sprite_=sprite;cx_=x;cy_=y;
  if(!sprite.runs||!sprite.count||sprite.count>area||x<-32768||x>32767||y<-32768||y>32767){phase_=RasterPhase::failed;return false;}
  phase_=RasterPhase::validating;return true;
 }
 void advance(){
  if(index_!=sprite_.count)return;
  if(phase_==RasterPhase::validating){
   if(position_!=area){phase_=RasterPhase::failed;return;}
   phase_=RasterPhase::drawing;index_=0;position_=0;
  }else if(phase_==RasterPhase::drawing)phase_=RasterPhase::complete;
 }
 public:
 RasterPhase phase()const{return phase_;}
 bool active()const{return phase_==RasterPhase::validating||phase_==RasterPhase::drawing;}
 void cancel(){*this=RasterJob{};}
 bool begin_sprite(const watcher_art::Sprite& sprite,int x,int y){return begin(sprite,x,y);}
 bool begin_ghost(State state,bool blink,int x,int y){
  const unsigned i=static_cast<unsigned>(state);
  if(i>=sizeof(watcher_art::sprites)/sizeof(watcher_art::sprites[0])){cancel();phase_=RasterPhase::failed;return false;}
  return begin(watcher_art::sprites[i][blink?1:0],x,y);
 }
 bool begin_personality(State state,bool blink,int x,int y,unsigned scale,unsigned hue,unsigned archetype){
  if(scale<32||scale>256||hue>359||archetype>3){cancel();phase_=RasterPhase::failed;return false;}
  if(!begin_ghost(state,blink,x,y))return false;
  const int widths[]={100,100,88,94},heights[]={100,90,100,94};
  personality_=true;hue_=hue;archetype_=archetype;
  xscale_=static_cast<int>(scale)*widths[archetype];yscale_=static_cast<int>(scale)*heights[archetype];
  return true;
 }
 template<class Sink> RasterProgress step(unsigned budget,Sink&& sink){
  RasterProgress result;
  while(result.work<budget&&active()){
   const auto run=sprite_.runs[index_++];const unsigned length=run.tag&0x7fff;
   ++result.work;
   if(phase_==RasterPhase::validating){
    if(position_>=area||!length||length>watcher_art::width-position_%watcher_art::width){phase_=RasterPhase::failed;break;}
    position_+=length;advance();continue;
   }
   const int sourceX=static_cast<int>(position_%watcher_art::width),sourceY=static_cast<int>(position_/watcher_art::width);
   position_+=length;
   if(!(run.tag&0x8000)){
    int left,right,top,bottom;uint16_t color=run.color;
    if(personality_){
     if(sourceY!=row_){
      row_=sourceY;const int dy=sourceY-static_cast<int>(watcher_art::origin_y);
      lean_=archetype_==3?dy*4/100:0;
      top_=cy_+dy*yscale_/25600;bottom_=cy_+(dy+1)*yscale_/25600;
      if(top_<0)top_=0;
      if(bottom_>static_cast<int>(logical_height))bottom_=logical_height;
     }
     top=top_;bottom=bottom_;
     left=cx_+(sourceX-static_cast<int>(watcher_art::origin_x)+lean_)*xscale_/25600;
     right=cx_+(sourceX+static_cast<int>(length)-static_cast<int>(watcher_art::origin_x)+lean_)*xscale_/25600;
    }else{
     left=cx_-static_cast<int>(watcher_art::origin_x)+sourceX;right=left+static_cast<int>(length);
     top=cy_-static_cast<int>(watcher_art::origin_y)+sourceY;bottom=top+1;
    }
    if(left<0)left=0;
    if(right>static_cast<int>(logical_width))right=logical_width;
    if(top<0)top=0;
    if(bottom>static_cast<int>(logical_height))bottom=logical_height;
    if(right>left&&bottom>top){
     if(personality_){
      const unsigned slot=(color^(color>>6))&63u;
      if(!occupied_[slot]||colors_[slot]!=color){colors_[slot]=color;tinted_[slot]=tint(color,hue_);occupied_[slot]=true;}
      color=tinted_[slot];
     }
     // Supported scales never enlarge a source row: at most ONE sink call.
     sink(left,top,right-left,color);++result.spans;
    }
   }
   advance();
  }
  result.phase=phase_;return result;
 }
};
static_assert(sizeof(RasterJob)<=512,"Bounded raster cursor RAM");
} // namespace watcher_render
