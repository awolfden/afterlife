// Single-owner retained panel framebuffer. Public pinned M5GFX APIs only.
// No extra canvas, sprite copy, band scheduler, worker task or vendor patch.
#pragma once
#include "retained-frame.h"
#include "display-transfer.h" // Shared DisplayFault values, not its controller.
namespace watcher_render {
template<class Display,class Framebuffer,class Bus,class Clock> class RetainedDisplay {
 Display& display_;Bus& bus_;Clock& clock_;Framebuffer* framebuffer_=nullptr;
 DisplayFault fault_=DisplayFault::none;
 bool started_=false,initialized_=false,pending_=false,writing_=false,full_required_=false;
 uint16_t background_=0;
 unsigned rotation_=0;
 int64_t last_clock_=-1,pending_since_=0;
 int32_t clip_x_=0,clip_y_=0,clip_w_=0,clip_h_=0;
 bool fail(DisplayFault value){fault_=value;return false;}
 bool stamp(int64_t& now){now=clock_();if(now<0||(last_clock_>=0&&now<last_clock_))return fail(DisplayFault::clock);last_clock_=now;return true;}
 bool configuration()const{return framebuffer_&&display_.getPanel()==framebuffer_&&framebuffer_->getBus()==&bus_&&!framebuffer_->getAutoDisplay()&&framebuffer_->getStartCount()==0&&framebuffer_->config().offset_rotation==0&&display_.width()==468&&display_.height()==468&&static_cast<unsigned>(display_.getColorDepth())==16&&static_cast<unsigned>(framebuffer_->getWriteDepth())==16&&static_cast<unsigned>(display_.getRotation())==rotation_;}
 bool exclusive(){if(!ready())return false;if(!configuration()||bus_.busy())return fail(DisplayFault::ownership);return true;}
 public:
 RetainedDisplay(Display& display,Bus& bus,Clock& clock):display_(display),bus_(bus),clock_(clock){}
 RetainedDisplay(const RetainedDisplay&)=delete;RetainedDisplay& operator=(const RetainedDisplay&)=delete;
 bool faulted()const{return fault_!=DisplayFault::none;}
 DisplayFault fault()const{return fault_;}
 bool pending()const{return pending_;}
 bool ready()const{return initialized_&&!faulted()&&!pending_&&!writing_;}
 void terminal(){fail(DisplayFault::submission);}
 bool initialize(Framebuffer& framebuffer,uint16_t background){
  if(started_)return false;
  started_=true;framebuffer_=&framebuffer;rotation_=display_.getRotation();background_=background;
  if(rotation_>=4||framebuffer_->getStartCount()!=0||bus_.busy())return fail(DisplayFault::ownership);
  framebuffer_->setAutoDisplay(false);
  if(!configuration())return fail(DisplayFault::configuration);
  // Reserve both ping-pong row buffers before the first dirty-union flush.
  if(!bus_.reserveDMABuffer(468*2))return fail(DisplayFault::reservation);
  pending_=true;fault_=DisplayFault::submission;
  display_.getClipRect(&clip_x_,&clip_y_,&clip_w_,&clip_h_);
  display_.setClipRect(0,0,468,468);display_.fillScreen(background);
  display_.setClipRect(clip_x_,clip_y_,clip_w_,clip_h_);
  framebuffer_->display(0,0,0,0);
  if(!stamp(pending_since_))return false;
  fault_=DisplayFault::none;return true;
 }
 bool begin_frame(const PaintRect& area){
  if(!exclusive())return false;
  if(area.empty()||area.x<0||area.y<0||area.right>466||area.bottom>466)return fail(DisplayFault::geometry);
  if(full_required_&&(area.x!=0||area.y!=0||area.right!=466||area.bottom!=466))return fail(DisplayFault::geometry);
  fault_=DisplayFault::submission;writing_=true;
  display_.getClipRect(&clip_x_,&clip_y_,&clip_w_,&clip_h_);
  display_.startWrite();
  if(full_required_){display_.setClipRect(0,0,468,468);display_.fillScreen(background_);full_required_=false;}
  display_.setClipRect(area.x,area.y,area.right-area.x,area.bottom-area.y);
  fault_=DisplayFault::none;return true;
 }
 bool submit_frame(){
  if(!writing_||faulted())return false;
  fault_=DisplayFault::submission;
  display_.endWrite();display_.setClipRect(clip_x_,clip_y_,clip_w_,clip_h_);writing_=false;
  if(!configuration()||bus_.busy())return fail(DisplayFault::ownership);
  pending_=true;
  // Flush the driver's actual accumulated physical dirty union exactly once.
  // Rotation is handled by the framebuffer's drawing APIs, not a second mapper.
  framebuffer_->display(0,0,0,0);
  if(!stamp(pending_since_))return false;
  fault_=DisplayFault::none;return true;
 }
 // Known RAM-only cancellation, not recovery from an unknown display fault.
 bool abandon_frame(){
  if(!writing_||pending_||faulted())return false;
  display_.endWrite();display_.setClipRect(clip_x_,clip_y_,clip_w_,clip_h_);writing_=false;
  if(!configuration()||bus_.busy())return fail(DisplayFault::ownership);
  full_required_=true;return true;
 }
 bool finish_band(){ // Shared boot-fence interface; this completes a whole frame.
  if(pending_&&!configuration())fail(DisplayFault::ownership);
  int64_t began=0;
  // Bounded remaining-row fence. Unlike the old renderer this can defer only
  // once per frame, not once for every 16-row band. No wrapper waitDMA calls.
  for(unsigned polls=0;polls<4096;++polls){
   if(!bus_.busy()){
    pending_=false;if(faulted()||!started_)return false;
    initialized_=true;return true;
   }
   if(faulted())return false;
   if(!pending_)return fail(DisplayFault::ownership);
   int64_t now=0;if(!stamp(now))return false;
   if(now-pending_since_>50000)return fail(DisplayFault::completion_timeout);
   if(!polls)began=now;
   if(now-began>=150)return false;
  }
  return false;
 }
 bool apply_rotation(unsigned rotation){
  if(rotation>=4)return fail(DisplayFault::configuration);
  if(!exclusive())return false;
  display_.setRotation(rotation);rotation_=rotation;full_required_=true;
  return configuration()||fail(DisplayFault::configuration);
 }
 bool apply_brightness(unsigned value){
  if(value>255)return fail(DisplayFault::configuration);
  if(!exclusive())return false;
  pending_=true;fault_=DisplayFault::submission;display_.setBrightness(value);
  if(!stamp(pending_since_))return false;
  fault_=DisplayFault::none;return true;
 }
};
}
