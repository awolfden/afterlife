// Portable transfer sequencing. No display, bus, clock or authority globals.
#pragma once
#include <cstdint>
#include "transfer-bands.h"
namespace watcher_render {
enum class TransferPhase { idle, ready, in_flight, complete, aborted, failed };
struct TransferProgress { bool submitted=false,completed_band=false; TransferPhase phase=TransferPhase::idle; };
// Adapter contract (not established by this class):
// ready_for_band(): checked DMA reservation/ownership, known CLEAN dirty union,
//                  valid buffers and no competing display writer.
// submit_band(): clipped full-sprite copy; restore clip; true only when every
//                row is known submitted under the pinned allocation invariant.
// finish_band(): underlying bus completion, NEVER the wrapper's no-op waits.
// Both submission and finish must have independently reviewed runtime budgets.
// Caller keeps source pixels/resources immutable while pending(), including
// after abort/failure. No new submission or publication follows a failure.
class TransferJob {
 TransferBands bands_;
 TransferPhase phase_=TransferPhase::idle;
 bool pending_=false,touched_=false,aborting_=false,pending_submitted_=false;
 unsigned submitted_=0,completed_=0;
 void finished(){
  pending_=false;if(pending_submitted_)++completed_;pending_submitted_=false;
  if(phase_==TransferPhase::failed)return;
  phase_=aborting_?TransferPhase::aborted:(bands_.complete()?TransferPhase::complete:TransferPhase::ready);
 }
 public:
 TransferJob()=default;
 TransferJob(const TransferJob&)=delete;TransferJob& operator=(const TransferJob&)=delete;
 TransferPhase phase()const{return phase_;}
 bool pending()const{return pending_;}
 bool touched()const{return touched_;}
 unsigned submitted_bands()const{return submitted_;}
 unsigned completed_bands()const{return completed_;}
 bool publishable()const{return phase_==TransferPhase::complete&&!pending_&&!aborting_;}
 bool begin(unsigned rotation,unsigned rows){
  if(pending_||phase_==TransferPhase::failed||phase_==TransferPhase::ready||phase_==TransferPhase::in_flight)return false;
  if(!bands_.begin(rotation,rows)){phase_=TransferPhase::failed;return false;}
  touched_=aborting_=false;submitted_=completed_=0;phase_=TransferPhase::ready;return true;
 }
 void abort(){
  aborting_=true;
  if(phase_!=TransferPhase::failed)phase_=pending_?TransferPhase::in_flight:TransferPhase::aborted;
 }
 template<class Adapter> TransferProgress step(Adapter& adapter){
  TransferProgress result;
  if(pending_){
   if(adapter.finish_band()){result.completed_band=pending_submitted_;finished();}
  }else if(phase_==TransferPhase::ready&&!aborting_){
   if(!adapter.ready_for_band()){phase_=TransferPhase::failed;}
   else{
    TransferBand band;
    if(!bands_.next(band)){
     phase_=bands_.complete()?TransferPhase::complete:TransferPhase::failed;
    }else{
     // Conservative before callback: an error/exception may leave partial
     // pixels or a last DMA row in flight. Drain, but never publish/retry.
     pending_=touched_=true;phase_=TransferPhase::failed;
     if(adapter.submit_band(band)){
      pending_submitted_=true;phase_=TransferPhase::in_flight;
      ++submitted_;result.submitted=true;
      if(adapter.finish_band()){finished();result.completed_band=true;}
     }
    }
   }
  }
  result.phase=phase_;return result;
 }
};
// Preserve the full sprite's stride/format. The destination clip, NOT a changed
// source image width, chooses the region. Restores clip even on host exceptions.
// This is copy/submission only; successful return is NOT transfer completion.
template<class Display,class Canvas>
bool copy_sprite_band(Display& display,Canvas& canvas,const TransferBand& band){
 if(band.x<0||band.y<0||band.width<=0||band.height<=0||band.x>466||band.y>466||band.width>466-band.x||band.height>466-band.y)return false;
 struct Restore {
  Display& display;int32_t x=0,y=0,w=0,h=0;
  explicit Restore(Display& value):display(value){display.getClipRect(&x,&y,&w,&h);}
  ~Restore(){display.setClipRect(x,y,w,h);}
  Restore(const Restore&)=delete;Restore& operator=(const Restore&)=delete;
 } restore(display);
 display.setClipRect(band.x,band.y,band.width,band.height);
 canvas.pushSprite(&display,0,0);return true;
}
} // namespace watcher_render
