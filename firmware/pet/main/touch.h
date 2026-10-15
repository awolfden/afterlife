#pragma once
#include "swipe.h"
#ifdef WATCHER_TOUCH_MEASURED_SCALE
#include "touch-normalization.h"
#endif
namespace watcher_touch {
// CST820 register protocol referenced from licensed M5GFX / LovyanGFX
// Touch_CST816S. See THIRD_PARTY_NOTICES.md. Only address 0x15 is accessed by
// the caller. No GPIO/IOE reset, calibration, NVS or second I2C driver.
#ifdef WATCHER_TOUCH_DIAGNOSTICS
struct SamplerStats {
 uint16_t setupAttempts=0,setupFailures=0,stageFailures[4]{},reads=0,ioErrors=0,invalid=0,contacts=0,releases=0,gapMs=0,maxGapMs=0;
 uint16_t invalidCount=0,invalidX=0,invalidY=0;
};
#endif
class Sampler {
 unsigned setup_=0;bool disabled_=false;int64_t next_=0,last_=-1;
#ifdef WATCHER_TOUCH_DIAGNOSTICS
 int64_t sampled_=-1;SamplerStats stats_{};
#endif
 public:
 bool ready()const{return setup_==4&&!disabled_;}
 bool halted()const{return disabled_;}
 unsigned stage()const{return setup_;}
#ifdef WATCHER_TOUCH_DIAGNOSTICS
 const SamplerStats& stats()const{return stats_;}
#endif
 template<class Read,class Write>
 bool poll(int64_t now,Sample& result,Read read,Write write){
  if(disabled_)return false;
  if(now<0||now>INT64_MAX-250||(last_>=0&&now<last_)){disabled_=true;result={};return true;}
  last_=now;if(now<next_)return false;
  next_=now+20;result={};
  if(setup_<4){
   uint8_t identity[3]{};
#ifdef WATCHER_TOUCH_DIAGNOSTICS
   saturate(stats_.setupAttempts);
#endif
   const bool ok=setup_==0?write(0x00,0x00):setup_==1?read(0xa7,identity,3):setup_==2?write(0xfa,0x20):write(0xed,20);
   if(!ok){
#ifdef WATCHER_TOUCH_DIAGNOSTICS
    saturate(stats_.setupFailures);saturate(stats_.stageFailures[setup_]);
#endif
    next_=now+250;
   }
   else ++setup_; // Retain a failed stage; sleeping-controller NACK is not permanent failure.
   return true; // Setup is never a release or a contact sample.
  }
#ifdef WATCHER_TOUCH_DIAGNOSTICS
  if(sampled_>=0){const auto gap=now-sampled_;stats_.gapMs=gap>UINT16_MAX?UINT16_MAX:static_cast<uint16_t>(gap);if(stats_.gapMs>stats_.maxGapMs)stats_.maxGapMs=stats_.gapMs;}
  sampled_=now;saturate(stats_.reads);
#endif
  uint8_t bytes[6]{};
  if(read(0x02,bytes,sizeof bytes)){
#ifdef WATCHER_TOUCH_MEASURED_SCALE
   const auto decoded=decode_measured_packet(bytes,sizeof bytes);
#else
   const auto decoded=decode_packet(bytes,sizeof bytes);
#endif
   result=decoded.sample;
#ifdef WATCHER_TOUCH_DIAGNOSTICS
   if(result.kind==Kind::error){
    saturate(stats_.invalid);
    if(decoded.rejection&reject_count)saturate(stats_.invalidCount);
    if(decoded.rejection&reject_x)saturate(stats_.invalidX);
    if(decoded.rejection&reject_y)saturate(stats_.invalidY);
   }
   else if(result.kind==Kind::contact)saturate(stats_.contacts);
   else saturate(stats_.releases);
#endif
  }
#ifdef WATCHER_TOUCH_DIAGNOSTICS
  else saturate(stats_.ioErrors);
#endif
  if(result.kind==Kind::error)next_=now+250; // No immediate retries or setup replay.
  return true;
 }
};
} // namespace watcher_touch
