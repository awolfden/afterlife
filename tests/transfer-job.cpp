#include "../firmware/render/transfer-job.h"
#include <cassert>
#include <iostream>
using namespace watcher_render;
struct Clip {int32_t x,y,w,h;bool operator==(const Clip& other)const{return x==other.x&&y==other.y&&w==other.w&&h==other.h;}};
struct Display {
 Clip clip{3,4,50,60};unsigned changes=0;
 void getClipRect(int32_t*x,int32_t*y,int32_t*w,int32_t*h){*x=clip.x;*y=clip.y;*w=clip.w;*h=clip.h;}
 void setClipRect(int32_t x,int32_t y,int32_t w,int32_t h){clip={x,y,w,h};++changes;}
};
struct Canvas {
 bool throws=false;unsigned pushes=0;
 void pushSprite(Display* display,int x,int y){assert(display&&x==0&&y==0);assert(display->clip.w>0&&display->clip.h>0);++pushes;if(throws)throw 42;}
};
struct Adapter {
 Display display;Canvas canvas;
 bool reserved=true,exclusive=true,dirty=false,busy=false,finishNow=true,submissionFails=false,holdLast=false;
 unsigned submissions=0,finishes=0;
 bool ready_for_band(){return reserved&&exclusive&&!dirty&&!busy;}
 bool submit_band(const TransferBand& band){
  assert(ready_for_band());++submissions;
  const auto old=display.clip;
  const bool copied=copy_sprite_band(display,canvas,band);assert(copied&&display.clip==old);
  if(submissionFails){dirty=true;busy=true;return false;}
  // Fake pinned flush: dirty union cleared after submission, last DMA pending.
  dirty=false;busy=true;
  if(holdLast&&band.physical_y+band.physical_rows==468)finishNow=false;
  return true;
 }
 bool finish_band(){++finishes;if(!finishNow)return false;busy=false;return true;}
};
int main(){
 unsigned successful=0;
 for(unsigned rotation=0;rotation<4;++rotation)for(unsigned rows:{2u,16u,64u}){
  TransferJob job;Adapter adapter;assert(job.begin(rotation,rows));unsigned steps=0;
  while(!job.publishable()){
   const unsigned before=adapter.submissions;const auto result=job.step(adapter);
   assert(adapter.submissions-before<=1);assert(result.phase!=TransferPhase::failed);assert(++steps<=235);
  }
  assert(job.submitted_bands()==job.completed_bands());assert(job.submitted_bands()==adapter.canvas.pushes);
  assert(!job.pending()&&job.touched());assert(adapter.display.clip==(Clip{3,4,50,60}));
  const unsigned before=adapter.submissions;job.step(adapter);assert(adapter.submissions==before);++successful;
 }
 // A push return must not publish while final DMA is outstanding.
 TransferJob delayed;Adapter slow;slow.finishNow=false;assert(delayed.begin(0,16));
 auto progress=delayed.step(slow);assert(progress.submitted&&!progress.completed_band&&delayed.pending()&&!delayed.publishable());
 for(int i=0;i<5;++i){delayed.step(slow);assert(slow.submissions==1&&delayed.completed_bands()==0);}
 assert(!delayed.begin(0,16));
 delayed.abort();assert(!delayed.begin(0,16));slow.finishNow=true;progress=delayed.step(slow);
 assert(progress.completed_band&&!delayed.pending()&&delayed.phase()==TransferPhase::aborted&&!delayed.publishable());
 assert(slow.submissions==1);assert(delayed.begin(0,16)); // Explicit fresh recovery, not automatic retry.
 // Final-band completion cannot revive a job invalidated by identity change.
 TransferJob last;Adapter final;final.holdLast=true;assert(last.begin(0,16));
 while(!last.pending()){last.step(final);assert(final.submissions<=30);}
 assert(final.submissions==30&&!last.publishable()&&last.completed_bands()==29);
 last.abort();final.finishNow=true;last.step(final);assert(last.completed_bands()==30&&!last.publishable());
 // Dirty unions/reservation/ownership violations stop BEFORE any new write.
 for(unsigned failure=0;failure<3;++failure){
  TransferJob job;Adapter bad;if(failure==0)bad.reserved=false;else if(failure==1)bad.exclusive=false;else bad.dirty=true;
  assert(job.begin(0,16));job.step(bad);assert(job.phase()==TransferPhase::failed&&!job.touched());
  assert(!bad.submissions&&!bad.canvas.pushes&&!job.publishable());assert(!job.begin(0,16));
 }
 TransferJob failed;Adapter reject;reject.submissionFails=true;assert(failed.begin(0,16));
 failed.step(reject);assert(failed.phase()==TransferPhase::failed&&failed.pending()&&failed.touched());
 progress=failed.step(reject);assert(!progress.completed_band&&!failed.pending()&&failed.completed_bands()==0&&failed.submitted_bands()==0);
 assert(!failed.publishable()&&!failed.begin(0,16)&&reject.submissions==1&&reject.dirty);
 // Host exception exercises clip restoration and conservative failed/in-flight state.
 TransferJob thrown;Adapter exception;exception.canvas.throws=true;assert(thrown.begin(0,16));
 try{thrown.step(exception);assert(false);}catch(int value){assert(value==42);}
 assert(exception.display.clip==(Clip{3,4,50,60}));assert(thrown.pending()&&thrown.phase()==TransferPhase::failed);
 thrown.step(exception);assert(!thrown.pending()&&!thrown.publishable()&&!thrown.completed_bands());
 Display display;Canvas canvas;const auto saved=display.clip;
 for(auto band:{TransferBand{-1,0,1,1,0,2},TransferBand{0,0,467,1,0,2},TransferBand{0,466,1,1,0,2},TransferBand{0,0,0,1,0,2},TransferBand{0,0,2147483647,1,0,2}}){assert(!copy_sprite_band(display,canvas,band));assert(display.clip==saved&&!canvas.pushes&&!display.changes);}
 TransferJob invalid;assert(!invalid.begin(4,16)&&invalid.phase()==TransferPhase::failed);assert(!invalid.begin(0,16));
 std::cout<<successful<<" complete transfers; pending DMA, dirty/reservation failure, clip restore and abort publication guards pass\n";
}
