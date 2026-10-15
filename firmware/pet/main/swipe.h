#pragma once
#include <cstdint>
#include <cstdlib>
namespace watcher_touch {
#ifdef WATCHER_TOUCH_DIAGNOSTICS
inline void saturate(uint16_t& value){if(value!=UINT16_MAX)++value;}
#endif
enum class Kind { error, released, contact };
struct Sample {Kind kind=Kind::error;int x=0,y=0;};
enum Reject : uint8_t { reject_packet=1,reject_count=2,reject_x=4,reject_y=8 };
struct Decoded {Sample sample{};unsigned rejection=0;};
inline Decoded decode_packet(const uint8_t* b,unsigned length){
 if(!b||length!=6)return {{},reject_packet};
 if(b[0]>1)return {{},reject_count};
 if(!b[0])return {{Kind::released,0,0},0};
 const int x=((b[1]&15)<<8)|b[2],y=((b[3]&15)<<8)|b[4];
 const unsigned rejection=(x>233?static_cast<unsigned>(reject_x):0u)|(y>233?static_cast<unsigned>(reject_y):0u);
 if(rejection)return {{},rejection};
 return {{Kind::contact,x,y},0};
}
inline Sample decode(const uint8_t* b,unsigned length){return decode_packet(b,length).sample;}
inline Sample orient(Sample sample,unsigned rotation){
 if(rotation>3)return {};
 if(sample.kind!=Kind::contact)return sample;
 if(sample.x<0||sample.x>233||sample.y<0||sample.y>233)return {};
 const int x=sample.x,y=sample.y;
 if(rotation==1){sample.x=y;sample.y=233-x;}
 else if(rotation==2){sample.x=233-x;sample.y=233-y;}
 else if(rotation==3){sample.x=233-y;sample.y=x;}
 return sample;
}
struct Context {
 bool enabled=false;uint32_t boot=0,shown=0,selected=0,count=0,rotation=0,source=0,generation=0,roster=0,selection=0;
};
struct Event {unsigned kind=0;uint32_t boot=0,shown=0;};
enum class CancelReason:unsigned { gap=1,context,packet,duration,vertical,release,unarmed,bounce,external };
#ifdef WATCHER_TOUCH_TIMING_DIAGNOSTICS
struct CancellationStats {uint16_t counts[8]{};uint16_t reason=0,release_flags=0,last_release_flags=0,release_counts[4]{};uint32_t at=0,last_release_at=0;};
#endif
class Swipe {
 bool armed_=false,tracking_=false;unsigned releases_=0,samples_=0;
 int x_=0,y_=0,dx_=0,dy_=0;int64_t last_=-1,started_=0,release_at_=-1;
 Context origin_{};
#ifdef WATCHER_TOUCH_TIMING_DIAGNOSTICS
 CancellationStats cancellations_{};
#endif
 void record(CancelReason reason,int64_t at){
#ifdef WATCHER_TOUCH_TIMING_DIAGNOSTICS
  const unsigned i=static_cast<unsigned>(reason);if(i>=1&&i<=8&&cancellations_.counts[i-1]!=65535)++cancellations_.counts[i-1];
  if(reason!=CancelReason::unarmed){cancellations_.reason=i;cancellations_.at=static_cast<uint32_t>(at>=0?at:(last_>=0?last_:0));}
#else
  (void)reason;(void)at;
#endif
 }
#ifdef WATCHER_TOUCH_DIAGNOSTICS
 uint16_t emitted_=0;
#endif
 static bool eligible(const Context& c){return c.enabled&&c.boot&&c.shown&&c.count>1&&c.selected<c.count&&c.rotation<4;}
 static bool same(const Context& a,const Context& b){return a.boot==b.boot&&a.selected==b.selected&&a.count==b.count&&a.rotation==b.rotation&&a.source==b.source&&a.generation==b.generation&&a.roster==b.roster&&a.selection==b.selection;}
 public:
 bool armed()const{return armed_;}
 bool tracking()const{return tracking_;}
 int displacement()const{return tracking_?dx_:0;}
 // Called even between polls. Lost samples never become a confirmed release.
 void expire(int64_t now){if(now<0||(last_>=0&&(now<last_||now-last_>100)))cancel(CancelReason::gap,now);}
#ifdef WATCHER_TOUCH_TIMING_DIAGNOSTICS
 const CancellationStats& cancellation_stats()const{return cancellations_;}
#endif
#ifdef WATCHER_TOUCH_DIAGNOSTICS
 uint16_t emitted()const{return emitted_;}
#endif
 void cancel(CancelReason reason=CancelReason::external,int64_t at=-1){if(armed_||tracking_||releases_)record(reason,at);armed_=tracking_=false;releases_=samples_=0;release_at_=-1;}
 // Called each loop, even when polling is suppressed. Heartbeat sequence alone
 // is not identity; the host additionally checks the captured selection epoch.
 bool guard(const Context& context,int64_t at=-1){
  if(!eligible(context)||(tracking_&&!same(origin_,context))){cancel(CancelReason::context,at);return false;}return true;
 }
 Event update(Sample raw,const Context& context,int64_t now){
  if(now<0||!guard(context,now)){cancel(CancelReason::context,now);last_=now;return {};}
  if(last_>=0&&(now<last_||now-last_>100)){cancel(CancelReason::gap,now);last_=now;return {};}
  if(last_==now)return {};
  last_=now;const auto sample=orient(raw,context.rotation);
  if(sample.kind==Kind::error){cancel(CancelReason::packet,now);return {};}
  if(sample.kind==Kind::released){
   if(releases_==0){releases_=1;release_at_=now;return {};}
   if(now-release_at_<20)return {};
   Event result{};
   if(tracking_&&samples_>=3&&release_at_-started_>=80&&release_at_-started_<=800&&std::abs(dx_)>=50&&std::abs(dx_)>=2*std::abs(dy_))result={dx_<0?2u:1u,origin_.boot,origin_.shown};
   if(tracking_&&!result.kind){
#ifdef WATCHER_TOUCH_TIMING_DIAGNOSTICS
    const unsigned flags=(samples_<3?1:0)|((release_at_-started_<80||release_at_-started_>800)?2:0)|(std::abs(dx_)<50?4:0)|(std::abs(dx_)<2*std::abs(dy_)?8:0);
    cancellations_.release_flags|=flags;cancellations_.last_release_flags=flags;cancellations_.last_release_at=static_cast<uint32_t>(now);
    for(unsigned i=0;i<4;i++)if((flags&(1u<<i))&&cancellations_.release_counts[i]!=65535)++cancellations_.release_counts[i];
#endif
    record(CancelReason::release,now);
   }
#ifdef WATCHER_TOUCH_DIAGNOSTICS
   if(result.kind)saturate(emitted_);
#endif
   tracking_=false;armed_=true;releases_=2;return result;
  }
  // A contact resuming during release confirmation is ambiguous, not a swipe.
  if(tracking_&&releases_){cancel(CancelReason::bounce,now);return {};}
  releases_=0;
  if(!armed_){record(CancelReason::unarmed,now);return {};}
  if(!tracking_){tracking_=true;origin_=context;started_=now;x_=sample.x;y_=sample.y;dx_=dy_=0;samples_=1;return {};}
  dx_=sample.x-x_;dy_=sample.y-y_;
  // Direct manipulation is reversible. Only the final signed displacement
  // qualifies release; duration, vertical and sample guards are unchanged.
  if(now-started_>800||std::abs(dy_)>25){cancel(now-started_>800?CancelReason::duration:CancelReason::vertical,now);return {};}
  ++samples_;return {};
 }
};
} // namespace watcher_touch
