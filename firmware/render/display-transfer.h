// Pinned M5GFX framebuffer adapter. Single owner; no vendor changes or tasks.
// Display/Canvas/Framebuffer/Bus use the pinned public APIs; Clock returns us.
// Preconditions NOT discoverable here: framebuffer init succeeded, no foreign
// writer or buffer release/resizing, and the pinned AMOLED allocation/flush
// implementation is unchanged. Call sites must establish those invariants.
#pragma once
#include <cstdint>
#include "transfer-job.h"
namespace watcher_render {
enum class DisplayFault { none, configuration, reservation, ownership, geometry, clock, submission, completion_timeout };
template<class Display,class Canvas,class Framebuffer,class Bus,class Clock>
class DisplayTransfer {
 Display& display_;Canvas& canvas_;Bus& bus_;Clock& clock_;Framebuffer* framebuffer_=nullptr;
 const unsigned max_rows_;
 DisplayFault fault_=DisplayFault::none;
 bool started_=false,initialized_=false,pending_=false,clean_=false;
 unsigned rotation_=0;
 int64_t last_clock_=-1,pending_since_=0;
 static constexpr int64_t completion_limit_us=50000;
 bool fail(DisplayFault reason){fault_=reason;clean_=false;return false;}
 bool stamp(int64_t& result){
  result=clock_();if(result<0||(last_clock_>=0&&result<last_clock_))return fail(DisplayFault::clock);
  last_clock_=result;return true;
 }
 bool configuration()const{
  return framebuffer_&&display_.getPanel()==framebuffer_&&framebuffer_->getBus()==&bus_&&
   framebuffer_->getAutoDisplay()&&framebuffer_->getStartCount()==0&&
   framebuffer_->config().offset_rotation==0&&
   display_.width()==468&&display_.height()==468&&canvas_.width()==466&&canvas_.height()==466&&
   static_cast<unsigned>(display_.getColorDepth())==16&&static_cast<unsigned>(canvas_.getColorDepth())==16&&
   static_cast<unsigned>(framebuffer_->getWriteDepth())==16&&canvas_.getBuffer()!=nullptr&&static_cast<unsigned>(display_.getRotation())==rotation_;
 }
 bool valid_band(const TransferBand& b)const{
  if(b.physical_y<0||(b.physical_y&1)||b.physical_rows<2||(b.physical_rows&1)||
     static_cast<unsigned>(b.physical_rows)>max_rows_||b.physical_y>468-b.physical_rows)return false;
  const int end=b.physical_y+b.physical_rows;
  const int low=(rotation_&2u)?468-end:b.physical_y;
  const int high=(rotation_&2u)?468-b.physical_y:end;
  if(low>=466)return false;
  const int extent=(high<466?high:466)-low;
  return (rotation_&1u)?(b.x==low&&b.y==0&&b.width==extent&&b.height==466):
                        (b.x==0&&b.y==low&&b.width==466&&b.height==extent);
 }
 public:
 DisplayTransfer(Display& display,Canvas& canvas,Bus& bus,Clock& clock,unsigned max_rows=16):display_(display),canvas_(canvas),bus_(bus),clock_(clock),max_rows_(max_rows){}
 DisplayTransfer(const DisplayTransfer&)=delete;DisplayTransfer& operator=(const DisplayTransfer&)=delete;
 DisplayFault fault()const{return fault_;}
 bool faulted()const{return fault_!=DisplayFault::none;}
 bool pending()const{return pending_;}
 // Raw idle is useful for shutdown, NOT permission to publish/reset after fault.
 bool bus_idle()const{return !bus_.busy();}
 bool ready()const{return initialized_&&!faulted()&&!pending_&&clean_;}
 // One-shot boot setup, BEFORE any framebuffer clear/copy. Full clear establishes
 // a clean dirty union only under checked reservation + exclusive ownership.
 // Returns successful submission, not completion. Caller services finish_band.
 bool initialize(Framebuffer& framebuffer,uint16_t background){
  if(started_||faulted())return false;
  started_=true;framebuffer_=&framebuffer;rotation_=display_.getRotation();
  if(rotation_>=4||max_rows_<2||max_rows_>64||(max_rows_&1u)||!configuration())return fail(DisplayFault::configuration);
  if(bus_.busy())return fail(DisplayFault::ownership);
  if(!bus_.reserveDMABuffer(468*2))return fail(DisplayFault::reservation);
  if(!stamp(pending_since_))return false;
  pending_=true;clean_=false;fault_=DisplayFault::submission;
  struct Restore {
   Display& display;int32_t x=0,y=0,w=0,h=0;
   explicit Restore(Display& d):display(d){d.getClipRect(&x,&y,&w,&h);}
   ~Restore(){display.setClipRect(x,y,w,h);}
  } restore(display_);
  display_.setClipRect(0,0,468,468);display_.fillScreen(background);
  // Timeout measures the remaining completion fence, not the synchronous boot
 // clear. Vendor waits INSIDE fill/push can still block on a hardware fault.
  if(!stamp(pending_since_))return false;
  fault_=DisplayFault::none;return true;
 }
 bool ready_for_band(){
  if(!ready())return false;
  if(!configuration()||bus_.busy())return fail(DisplayFault::ownership);
  return true;
 }
 bool submit_band(const TransferBand& band){
  if(!ready_for_band())return false;
  if(!valid_band(band))return fail(DisplayFault::geometry);
  if(!stamp(pending_since_))return false;
  // Conservative on host exceptions/unknown partial submission. Firmware uses
 // no exceptions. Never clear a real fault or retry an unknown dirty union.
  pending_=true;clean_=false;fault_=DisplayFault::submission;
  if(!copy_sprite_band(display_,canvas_,band))return false;
  if(!stamp(pending_since_))return false;
  fault_=DisplayFault::none;return true;
 }
 bool finish_band(){
  // Bounded raw completion fence: <=128 polls and100us plus one poll/clock
  // cost. One poll alone would force nearly every final DMA row into the next
  // outer-loop tick (~300ms/frame at16 rows). No wrapper waits or resubmission.
  if(pending_&&!configuration())fail(DisplayFault::ownership);
  int64_t began=0;
  for(unsigned poll=0;poll<128;++poll){
   if(!bus_.busy()){
    pending_=false;
    if(faulted())return false; // Terminal: never allow late completion to publish.
    if(!started_)return false;
    initialized_=true;clean_=true;return true;
   }
   if(faulted())return false;
   if(!pending_)return fail(DisplayFault::ownership);
   int64_t now=0;if(!stamp(now))return false;
   if(now-pending_since_>completion_limit_us)return fail(DisplayFault::completion_timeout);
   if(!poll)began=now;
   if(now-began>=100)return false;
  }
  return false;
 }
 // Only after abort+drain at the controller layer. Deferred requests must not
 // mutate mapping of an outstanding band. No framebuffer dirtying here.
 bool apply_rotation(unsigned rotation){
  if(rotation>=4)return fail(DisplayFault::configuration);
  if(!ready_for_band())return false;
  display_.setRotation(rotation);rotation_=rotation;
  if(!configuration())return fail(DisplayFault::configuration);
  return true;
 }
 bool apply_brightness(unsigned value){
  if(value>255)return fail(DisplayFault::configuration);
  if(!ready_for_band())return false;
  pending_=true;clean_=false;fault_=DisplayFault::submission;
  display_.setBrightness(value);
  if(!stamp(pending_since_))return false;
  fault_=DisplayFault::none;return true;
 }
 // Any fault is TERMINAL until board reinitialization. Even after the bus goes
 // idle, finish_band stays false so a TransferJob cannot accidentally publish.
 // Resources stay pinned; caller must invalidate authority and stop new work.
 // Brightness/reset/shutdown traffic must also be sequenced by the sole owner.
};
} // namespace watcher_render
