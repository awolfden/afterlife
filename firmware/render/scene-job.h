// Owned scene commands + cooperative offscreen rasterization. No driver/input IO.
#pragma once
#include <cstdint>
#include "raster-job.h"
#include "carousel.h"
namespace watcher_render {
enum class SceneKind : uint8_t { ghost, shadow, outline_circle, filled_circle, text, afterlife };
struct SceneOp {
 SceneKind kind=SceneKind::shadow;
 int16_t x=0,y=0,radius=0;uint16_t clip_top=0,clip_bottom=466;float size=1;
 uint16_t color=0,scale=256,hue=0,text_background=0x0842;
 uint8_t state=0,alpha=255,archetype=0,text_slot=0;
 bool blink=false,personality=false,family=false;
 uint8_t character=0;
};
class SceneProgram {
 static constexpr unsigned capacity=40,text_capacity=8,text_bytes=64;
 SceneOp ops_[capacity]{};char texts_[text_capacity][text_bytes]{};
 unsigned count_=0,text_count_=0;bool valid_=true;
 bool append(const SceneOp& op){if(!valid_||count_==capacity){valid_=false;return false;}ops_[count_++]=op;return true;}
 public:
 // Presentation only, copied with the owned scene; never session/control authority.
 int8_t family_selection=-1;
 bool valid()const{return valid_;}
 unsigned count()const{return count_;}
 const SceneOp& op(unsigned i)const{return ops_[i];} // Job accesses only i<count.
 const char* text_at(unsigned i)const{return texts_[i];} // Owned slot from validated op.
 bool ghost(State state,bool blink,int x,int y,bool personality=false,unsigned scale=256,unsigned hue=0,unsigned archetype=0,unsigned alpha=255){
  if(static_cast<unsigned>(state)>8||x<-1024||x>1024||y<-1024||y>1024||scale<32||scale>256||hue>359||archetype>3||alpha>255){valid_=false;return false;}
  SceneOp op;op.kind=SceneKind::ghost;op.state=static_cast<uint8_t>(state);op.blink=blink;op.x=x;op.y=y;op.personality=personality;op.scale=scale;op.hue=hue;op.archetype=archetype;op.alpha=alpha;return append(op);
 }
 bool family_ghost(State state,bool blink,int x,int y,unsigned character,unsigned scale=256,unsigned alpha=255){
#ifdef WATCHER_GHOST_FAMILY
  if(character>5){valid_=false;return false;}
  // The family's palette and silhouette are never hue-rotated or distorted.
  if(!ghost(state,blink,x,y,true,scale,270,0,alpha))return false;
  auto& op=ops_[count_-1];op.family=true;op.character=static_cast<uint8_t>(character);return true;
#else
  (void)state;(void)blink;(void)x;(void)y;(void)character;(void)scale;(void)alpha;
  valid_=false;return false;
#endif
 }
 bool afterlife(){
#ifdef WATCHER_AFTERLIFE
  SceneOp op;op.kind=SceneKind::afterlife;return append(op);
#else
  valid_=false;return false;
#endif
 }
 bool shadow(){return append(SceneOp{});}
 bool circle(int x,int y,unsigned radius,uint16_t color,bool filled){
  if(x<-1024||x>1024||y<-1024||y>1024||radius<1||radius>6){valid_=false;return false;}
  SceneOp op;op.kind=filled?SceneKind::filled_circle:SceneKind::outline_circle;op.x=x;op.y=y;op.radius=radius;op.color=color;return append(op);
 }
 bool text(const char* value,int y,float size,uint16_t color,uint16_t background=0x0842,unsigned clip_top=0,unsigned clip_bottom=466){
#ifndef WATCHER_AFTERLIFE
  if(background!=0x0842){valid_=false;return false;}
#endif
  if(!value||clip_top>=clip_bottom||clip_bottom>466||text_count_==text_capacity||y<0||y>=466||!(size>=1&&size<=3)){valid_=false;return false;}
  unsigned length=0;while(length<text_bytes&&value[length])++length;
  if(length==text_bytes){valid_=false;return false;} // Never silently truncate.
  SceneOp op;op.kind=SceneKind::text;op.y=y;op.size=size;op.color=color;op.text_background=background;op.text_slot=text_count_;op.clip_top=clip_top;op.clip_bottom=clip_bottom;
  if(!append(op))return false;
  for(unsigned i=0;i<=length;++i)texts_[text_count_][i]=value[i];
  ++text_count_;return true;
 }
};
static_assert(sizeof(SceneProgram)<=2048,"Owned scene command RAM bound");
enum class ScenePhase { idle, clearing, drawing, complete, failed };
struct SceneProgress {unsigned work=0,spans=0;ScenePhase phase=ScenePhase::idle;};
// Surface methods: clear_rows(y,height,background), span(x,y,length,color),
// shadow(), circle(x,y,radius,color,filled), text(value,y,size,color).
// Each unit is <=8 clear rows, one visited RLE run, one ghost setup, or one
// bounded primitive/string. Work counts are NOT a hardware time guarantee.
// Font0 text is at most63 bytes, size<=3, circle radius<=6, shadow fixed60x9.
// Caller applies wall-clock + work caps and returns to the ordinary outer loop.
class SceneJob {
 SceneProgram program_;RasterJob raster_;
 ScenePhase phase_=ScenePhase::idle;
 unsigned row_=0,index_=0;bool raster_active_=false;
 uint16_t background_=0x0842;
 public:
 SceneJob()=default;SceneJob(const SceneJob&)=delete;SceneJob& operator=(const SceneJob&)=delete;
 ScenePhase phase()const{return phase_;}
 bool complete()const{return phase_==ScenePhase::complete;}
 bool begin(const SceneProgram& program,uint16_t background=0x0842){
  if(phase_==ScenePhase::clearing||phase_==ScenePhase::drawing)return false;
  if(!program.valid()||!program.count()){phase_=ScenePhase::failed;return false;}
  // Family scenes are supported by RetainedFrame only, never silently painted
  // as a legacy flagship by this compatibility renderer.
  for(unsigned i=0;i<program.count();++i)if(program.op(i).family||program.op(i).kind==SceneKind::afterlife||program.op(i).text_background!=0x0842||program.op(i).clip_top!=0||program.op(i).clip_bottom!=466){phase_=ScenePhase::failed;return false;}
  program_=program;background_=background;row_=index_=0;raster_active_=false;raster_.cancel();phase_=ScenePhase::clearing;return true;
 }
 void cancel(){raster_.cancel();raster_active_=false;phase_=ScenePhase::idle;}
 template<class Surface> SceneProgress step(unsigned budget,Surface& surface){
  SceneProgress result;
  while(result.work<budget&&(phase_==ScenePhase::clearing||phase_==ScenePhase::drawing)){
   if(phase_==ScenePhase::clearing){
    const unsigned rows=466-row_<8?466-row_:8;
    phase_=ScenePhase::failed; // A throwing host surface cannot resume partial work.
    surface.clear_rows(row_,rows,background_);row_+=rows;++result.work;
    phase_=row_==466?ScenePhase::drawing:ScenePhase::clearing;
    continue;
   }
   const auto& op=program_.op(index_);
   if(op.kind==SceneKind::ghost){
    if(!raster_active_){
     const auto state=static_cast<State>(op.state);
     const bool begun=op.personality?raster_.begin_personality(state,op.blink,op.x,op.y,op.scale,op.hue,op.archetype):raster_.begin_ghost(state,op.blink,op.x,op.y);
     ++result.work;if(!begun){phase_=ScenePhase::failed;break;}raster_active_=true;continue;
    }
    phase_=ScenePhase::failed;
    const auto progress=raster_.step(budget-result.work,[&](int x,int y,int length,uint16_t color){surface.span(x,y,length,carousel_color(color,background_,op.alpha));});
    result.work+=progress.work;result.spans+=progress.spans;
    if(progress.phase==RasterPhase::failed)break;
    phase_=ScenePhase::drawing;
    if(progress.phase!=RasterPhase::complete)continue;
    raster_active_=false;
   }else{
    ++result.work;phase_=ScenePhase::failed;
    switch(op.kind){
     case SceneKind::shadow:surface.shadow();break;
     case SceneKind::outline_circle:case SceneKind::filled_circle:surface.circle(op.x,op.y,op.radius,op.color,op.kind==SceneKind::filled_circle);break;
     case SceneKind::text:surface.text(program_.text_at(op.text_slot),op.y,op.size,op.color);break;
     default:result.phase=phase_;return result;
    }
    phase_=ScenePhase::drawing;
   }
   if(phase_==ScenePhase::failed)break;
   if(++index_==program_.count())phase_=ScenePhase::complete;
  }
  result.phase=phase_;return result;
 }
};
} // namespace watcher_render
