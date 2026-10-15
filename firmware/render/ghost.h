// Original portable rendering core. No driver, storage, transport or notification calls.
#pragma once
#include <stdint.h>
#include <cmath>
#include "../art/ghost.generated.h"
namespace watcher_render {
enum class State : uint8_t { idle, thinking, working, needs_input, child_help, completed, failed, unavailable, offline };
constexpr unsigned logical_width=466, logical_height=466;
inline bool valid(const watcher_art::Sprite& sprite) {
  constexpr unsigned area=watcher_art::width*watcher_art::height;
  if(!sprite.runs || !sprite.count || sprite.count>area) return false;
  unsigned position=0;
  for(size_t i=0;i<sprite.count;i++) {
    const unsigned length=sprite.runs[i].tag&0x7fff;
    if(position>=area || !length || length>watcher_art::width-position%watcher_art::width) return false;
    position+=length;
  }
  return position==area;
}
// sink(x,y,length,rgb565) receives only visible, display-bounded horizontal spans.
// Decode into a RAM canvas; do not assume one SPI transaction per span is performant.
// Full prevalidation means malformed data cannot leave a partially drawn character.
template<class Sink>
bool draw_sprite(const watcher_art::Sprite& sprite, int16_t center_x, int16_t center_y, Sink&& sink) {
  if(!valid(sprite)) return false;
  unsigned position=0;
  for(size_t i=0;i<sprite.count;i++) {
    const auto& run=sprite.runs[i];const unsigned length=run.tag&0x7fff;
    int x=static_cast<int>(center_x)-static_cast<int>(watcher_art::origin_x)+static_cast<int>(position%watcher_art::width);
    const int y=static_cast<int>(center_y)-static_cast<int>(watcher_art::origin_y)+static_cast<int>(position/watcher_art::width);
    position+=length;
    if((run.tag&0x8000) || y<0 || y>=static_cast<int>(logical_height)) continue;
    int end=x+static_cast<int>(length);if(x<0)x=0;if(end>static_cast<int>(logical_width))end=logical_width;
    if(end>x)sink(x,y,end-x,run.color);
  }
  return true;
}
template<class Sink>
bool draw_ghost(State state, bool blink, int16_t center_x, int16_t center_y, Sink&& sink) {
  const unsigned index=static_cast<unsigned>(state);
  if(index>=sizeof(watcher_art::sprites)/sizeof(watcher_art::sprites[0])) return false;
  return draw_sprite(watcher_art::sprites[index][blink?1:0],center_x,center_y,sink);
}
// Recolor only the original purple body, preserving neutral eyes/mouth and blush.
constexpr unsigned original_purple_hue=270;
inline bool multi_session_layout(unsigned count){return count>1;}
inline uint16_t tint(uint16_t color,unsigned hue){
 if(hue==original_purple_hue)return color; // Exact original art, not an approximate hue rotation.
 const unsigned r=((color>>11)&31)*255/31,g=((color>>5)&63)*255/63,b=(color&31)*255/31;
 if(!(b>r&&r>g))return color;
 const unsigned sector=(hue%360)/60,f=(hue%60),d=b-g,x=(sector%2)?d*(60-f)/60:d*f/60;
 unsigned rr=g,gg=g,bb=g;
 switch(sector){case 0:rr=b;gg+=x;break;case 1:rr+=x;gg=b;break;case 2:gg=b;bb+=x;break;case 3:gg+=x;bb=b;break;case 4:rr+=x;bb=b;break;default:rr=b;bb+=x;}
 return static_cast<uint16_t>(((rr*31/255)<<11)|((gg*63/255)<<5)|(bb*31/255));
}
struct Pose {int x=0,y=0;bool blink=false;};
inline Pose personality_pose(State state,bool reduced,int64_t now,unsigned archetype,bool small){
 const auto value=static_cast<unsigned>(state);
 if(reduced||now<0||archetype>3||value>=7)return {};
 const float t=now/1000.0f,phase=archetype*0.85f;
 const float pace=(0.75f+archetype*0.17f)*(value==2?1.15f:value==1?0.8f:value==5?0.5f:0.65f);
 const int periods[]={5700,6500,4900,7300};
 return {archetype?static_cast<int>(std::sin(t*pace*0.7f+phase)*(small?1:2)):0,
         static_cast<int>(std::sin(t*pace+phase)*(small?4:5)),
         (now%periods[archetype])>periods[archetype]-180};
}
template<class Sink>
bool draw_personality(State state,bool blink,int center_x,int center_y,unsigned scale,unsigned hue,unsigned archetype,Sink&& sink){
 if(scale<32||scale>256||hue>359||archetype>3)return false;
 // Preserve every state-specific face. One span pass, no extra frame allocations
 // or decode: original, compact, slender, and gently leaning silhouettes.
 const int widths[]={100,100,88,94},heights[]={100,90,100,94};
 const int xscale=static_cast<int>(scale)*widths[archetype],yscale=static_cast<int>(scale)*heights[archetype];
 // Per-call bounded memoization only; no shared state or heap allocation.
 int row=-1,top=0,bottom=0,lean=0;
 uint16_t colors[64]{},tinted[64]{};bool occupied[64]{};
 return draw_ghost(state,blink,watcher_art::origin_x,watcher_art::origin_y,[&](int x,int y,int n,uint16_t color){
  if(y!=row){
   row=y;const int dy=y-static_cast<int>(watcher_art::origin_y);
   lean=archetype==3?dy*4/100:0;
   top=center_y+dy*yscale/25600;bottom=center_y+(dy+1)*yscale/25600;
   if(top<0)top=0;
   if(bottom>static_cast<int>(logical_height))bottom=logical_height;
  }
  if(bottom<=top)return; // Shrunk-away rows need no X scaling or tint work.
  int left=center_x+(x-static_cast<int>(watcher_art::origin_x)+lean)*xscale/25600;
  int right=center_x+(x+n-static_cast<int>(watcher_art::origin_x)+lean)*xscale/25600;
  if(left<0)left=0;
  if(right>static_cast<int>(logical_width))right=logical_width;
  if(right<=left)return;
  const unsigned slot=(color^(color>>6))&63u;
  if(!occupied[slot]||colors[slot]!=color){colors[slot]=color;tinted[slot]=tint(color,hue);occupied[slot]=true;}
  for(int yy=top;yy<bottom;yy++)sink(left,yy,right-left,tinted[slot]);
 });
}
// Existing callers retain the original silhouette and rendering contract.
template<class Sink>
bool draw_personality(State state,bool blink,int center_x,int center_y,unsigned scale,unsigned hue,Sink&& sink){
 return draw_personality(state,blink,center_x,center_y,scale,hue,0,sink);
}
} // namespace watcher_render
