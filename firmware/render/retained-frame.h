// Retained composition: one immutable scene, one dirty union, no frame queue.
#pragma once
#include <algorithm>
#include <cstring>
#include "scene-job.h" // Shared owned scene commands; SceneJob is not used here.
#ifdef WATCHER_GHOST_FAMILY
#include "family-art.h"
#endif
#ifdef WATCHER_AFTERLIFE
#include "afterlife-art.h"
#include "connecting-pose.h"
#endif
namespace watcher_render {
struct PaintRect {
 int x=0,y=0,right=0,bottom=0;
 bool empty()const{return x>=right||y>=bottom;}
 unsigned area()const{return empty()?0:static_cast<unsigned>((right-x)*(bottom-y));}
 PaintRect clipped()const{return {std::max(0,x),std::max(0,y),std::min(466,right),std::min(466,bottom)};}
 bool intersects(const PaintRect& r)const{return !empty()&&!r.empty()&&x<r.right&&right>r.x&&y<r.bottom&&bottom>r.y;}
 void add(PaintRect r){r=r.clipped();if(r.empty())return;if(empty()){*this=r;return;}x=std::min(x,r.x);y=std::min(y,r.y);right=std::max(right,r.right);bottom=std::max(bottom,r.bottom);}
};
inline PaintRect paint_bounds(const SceneOp& op){
 switch(op.kind){
 case SceneKind::afterlife:return {0,0,466,466};
 case SceneKind::ghost:{
  // Conservative bounds include archetype lean, integer truncation and source
  // transparent borders. They deliberately do not depend on sprite content.
  const int scale=op.personality?op.scale:256;
  const int rx=(146*scale+255)/256+1,top=(131*scale+255)/256+1,down=(151*scale+255)/256+1;
  return PaintRect{op.x-rx,op.y-top,op.x+rx+1,op.y+down+1}.clipped();
 }
 case SceneKind::shadow:return {173,311,294,330};
 case SceneKind::outline_circle:case SceneKind::filled_circle:return PaintRect{op.x-op.radius,op.y-op.radius,op.x+op.radius+1,op.y+op.radius+1}.clipped();
 case SceneKind::text:return PaintRect{0,std::max<int>(op.y,op.clip_top),466,std::min<int>(op.clip_bottom,op.y+static_cast<int>(std::ceil(op.size))*10+4)}.clipped();
 }
 return {};
}
inline bool same_paint(const SceneProgram& a,unsigned i,const SceneProgram& b,unsigned j){
 const auto& x=a.op(i);const auto& y=b.op(j);
 if(x.kind!=y.kind||x.x!=y.x||x.y!=y.y||x.radius!=y.radius||x.size!=y.size||x.color!=y.color||x.scale!=y.scale||x.hue!=y.hue||x.state!=y.state||x.alpha!=y.alpha||x.archetype!=y.archetype||x.blink!=y.blink||x.personality!=y.personality||x.family!=y.family||x.character!=y.character||x.text_background!=y.text_background||x.clip_top!=y.clip_top||x.clip_bottom!=y.clip_bottom)return false;
 return x.kind!=SceneKind::text||std::strcmp(a.text_at(x.text_slot),b.text_at(y.text_slot))==0;
}
// Immutable generated art is checked ONCE, before any frame can use it. Indexing
// makes vertically clipped and scale-eliminated rows O(1), not an RLE traversal.
class SpriteRows {
 uint32_t rows_[9][2][watcher_art::height+1]{};
 bool ready_=false;
 public:
 bool ready()const{return ready_;}
 bool initialize(){
  if(ready_)return true;
#ifdef WATCHER_AFTERLIFE
  if(!valid_afterlife_art())return false;
#endif
#ifdef WATCHER_GHOST_FAMILY
  if(!valid_family_art())return false;
#endif
  for(unsigned s=0;s<9;++s)for(unsigned b=0;b<2;++b){
   const auto& sprite=watcher_art::sprites[s][b];
   if(!valid(sprite))return false;
   unsigned x=0,y=0;
   for(unsigned i=0;i<sprite.count;++i){if(x==0)rows_[s][b][y]=i;x+=sprite.runs[i].tag&0x7fff;if(x==watcher_art::width){x=0;++y;}}
   rows_[s][b][watcher_art::height]=static_cast<uint32_t>(sprite.count);
  }
  ready_=true;return true;
 }
 template<class Surface> bool draw(const SceneOp& op,const PaintRect& clip,uint16_t background,Surface& surface,uint32_t& visited)const{
  if(!ready_||op.state>8||op.archetype>3||op.scale<32||op.scale>256||op.hue>359||op.character>5)return false;
#ifndef WATCHER_GHOST_FAMILY
  if(op.family)return false;
#endif
  if(op.family&&(!op.personality||op.hue!=270||op.archetype!=0))return false;
  const auto& sprite=watcher_art::sprites[op.state][op.blink?1:0];
  const auto* offsets=rows_[op.state][op.blink?1:0];
  constexpr int widths[]={100,100,88,94},heights[]={100,90,100,94};
  const int xs=op.personality?op.scale*widths[op.archetype]:25600,ys=op.personality?op.scale*heights[op.archetype]:25600;
  uint16_t colors[64]{},tinted[64]{};bool occupied[64]{};
  for(unsigned row=0;row<watcher_art::height;++row){
   const int dy=static_cast<int>(row)-static_cast<int>(watcher_art::origin_y);
   const int top=std::max(clip.y,op.y+dy*ys/25600),bottom=std::min(clip.bottom,op.y+(dy+1)*ys/25600);
   if(bottom<=top)continue;
   const int lean=op.personality&&op.archetype==3?dy*4/100:0;
   int x=0;const watcher_art::Run* runs=sprite.runs;
   unsigned begin=offsets[row],end=offsets[row+1];
#ifdef WATCHER_GHOST_FAMILY
   if(op.family&&op.character){
    const auto shared=watcher_family::rows[watcher_family::row_ids[op.character-1][op.state][op.blink?1:0][row]];
    runs=watcher_family::runs;begin=shared.offset;end=shared.offset+shared.count;
   }
#endif
   for(unsigned i=begin;i<end;++i){
    const auto& run=runs[i];const int length=run.tag&0x7fff;++visited;
    const int left=std::max(clip.x,op.x+(x-static_cast<int>(watcher_art::origin_x)+lean)*xs/25600);
    x+=length;
    const int right=std::min(clip.right,op.x+(x-static_cast<int>(watcher_art::origin_x)+lean)*xs/25600);
    if((run.tag&0x8000)||right<=left)continue;
    const unsigned slot=(run.color^(run.color>>6))&63u;
    if(!occupied[slot]||colors[slot]!=run.color){occupied[slot]=true;colors[slot]=run.color;tinted[slot]=carousel_color(op.personality?tint(run.color,op.hue):run.color,background,op.alpha);}
    for(int y=top;y<bottom;++y)surface.span(left,y,right-left,tinted[slot]);
   }
  }
  return true;
 }
};
class RetainedFrame {
 SceneProgram previous_{},next_{};
 PaintRect dirty_{};
 bool retained_=false,active_=false,painted_=false,failed_=false;
 uint32_t visited_=0;
 public:
 void reset(){retained_=active_=painted_=failed_=false;dirty_={};}
 bool active()const{return active_;}
 PaintRect dirty()const{return dirty_;}
 uint32_t visited_runs()const{return visited_;}
 bool prepare(const SceneProgram& scene){
  if(active_||failed_||!scene.valid()||!scene.count())return false;
  next_=scene;dirty_={};visited_=0;painted_=false;
  if(!retained_)dirty_={0,0,466,466};
  else for(unsigned i=0;i<std::max(previous_.count(),next_.count());++i){
   if(i<previous_.count()&&i<next_.count()&&same_paint(previous_,i,next_,i))continue;
   if(i<previous_.count())dirty_.add(paint_bounds(previous_.op(i)));
   if(i<next_.count())dirty_.add(paint_bounds(next_.op(i)));
  }
  active_=true;return true;
 }
 template<class Surface> bool paint(const SpriteRows& sprites,uint16_t background,Surface& surface){
  if(!active_||painted_||!sprites.ready())return false;
  // No retries after an exception or partial surface failure.
  active_=false;failed_=true;
  if(!dirty_.empty()){
   surface.clip(dirty_);surface.clear(dirty_,background);
   for(unsigned i=0;i<next_.count();++i){
    const auto& op=next_.op(i);if(!paint_bounds(op).intersects(dirty_))continue;
    switch(op.kind){
     case SceneKind::afterlife:
#ifdef WATCHER_AFTERLIFE
      draw_afterlife(dirty_.x,dirty_.y,dirty_.right,dirty_.bottom,surface,visited_);break;
#else
      return false;
#endif
     case SceneKind::ghost:if(!sprites.draw(op,dirty_,background,surface,visited_))return false;break;
     case SceneKind::shadow:surface.shadow();break;
     case SceneKind::outline_circle:case SceneKind::filled_circle:surface.circle(op.x,op.y,op.radius,op.color,op.kind==SceneKind::filled_circle);break;
     case SceneKind::text:{
      const bool clipped=op.clip_top!=0||op.clip_bottom!=466;
      if(clipped)surface.clip({dirty_.x,std::max<int>(dirty_.y,op.clip_top),dirty_.right,std::min<int>(dirty_.bottom,op.clip_bottom)});
#ifdef WATCHER_AFTERLIFE
      if(op.text_background!=0x0842)surface.text_background(next_.text_at(op.text_slot),op.y,op.size,op.color,op.text_background);
      else
#endif
      surface.text(next_.text_at(op.text_slot),op.y,op.size,op.color);
      if(clipped)surface.clip(dirty_);
      break;
     }
    }
   }
  }
  painted_=active_=true;failed_=false;return true;
 }
 // Only after the matching snapshot was published following actual DMA idle.
 bool commit(){if(!active_||!painted_)return false;previous_=next_;retained_=true;active_=painted_=false;return true;}
};
}
