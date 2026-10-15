// Display-publication authority for a single-owner resumable render job.
// Device revocation is injected through a REQUIRED pre-loss hook. Bus completion
// and live voice/mute checks belong to the caller. 'Coherent' means usable last-completed identity, NOT a claim
// of pixel-atomic transfer or physical scanout during same-identity band updates.
#pragma once
#include <cstdint>
namespace watcher_render {
struct RenderIdentity {
 uint32_t boot=0,source=0,generation=0,selected=0,count=0,rotation=0,roster=0,selection=0;
 bool linked=false;
 uint32_t view=0; // Local UI/settings generation; never a host selection epoch.
 bool operator==(const RenderIdentity& b)const{return boot==b.boot&&source==b.source&&generation==b.generation&&selected==b.selected&&count==b.count&&rotation==b.rotation&&roster==b.roster&&selection==b.selection&&linked==b.linked&&view==b.view;}
 bool valid()const{return boot&&rotation<4&&count<=16&&(count?selected<count:selected==0);}
};
struct RenderSnapshot {
 RenderIdentity identity{};
 uint32_t seq=0,paging_event=0,event_ack=0;
 bool preview=false,local=false; // Local settings pixels cannot grant session ACK authority.
};
class RenderAuthority {
 RenderSnapshot visible_{},job_{};
 bool coherent_=false,active_=false,invalid_=false,pixels_=false,transfer_ambiguous_=false;
 void (*before_loss_)(void*) noexcept=nullptr;void* context_=nullptr;
 bool retired_display(const RenderIdentity& live,uint32_t seq)const{
  return coherent_&&(!live.valid()||visible_.identity.boot!=live.boot||visible_.identity.source!=live.source||visible_.identity.generation!=live.generation||(visible_.identity.linked&&!live.linked)||(live.linked&&seq<visible_.seq));
 }
 void lose_authority(){
  if(shown())before_loss_(context_); // Prior shown identity remains available to cancel.
  coherent_=false;transfer_ambiguous_=true;
 }
 public:
 // Hook may read prior authority but must not mutate/reenter transitions.
 // Voice-disabled callers pass an explicit no-op.
 RenderAuthority(void (*before_loss)(void*) noexcept,void* context):before_loss_(before_loss),context_(context){}
 RenderAuthority(const RenderAuthority&)=delete;RenderAuthority& operator=(const RenderAuthority&)=delete;
 bool active()const{return active_;}
 bool invalid()const{return invalid_;}
 bool pixels_started()const{return pixels_;}
 bool coherent()const{return coherent_;}
 uint32_t shown()const{return coherent_&&visible_.identity.linked&&!visible_.local?visible_.seq:0;}
 const RenderSnapshot& completed()const{return visible_;}
 const RenderSnapshot& snapshot()const{return job_;}
 bool start(const RenderSnapshot& snapshot){
  if(!before_loss_||active_||!snapshot.identity.valid()||(snapshot.identity.linked&&!snapshot.seq))return false;
  if(coherent_&&snapshot.identity.boot==visible_.identity.boot&&snapshot.identity.linked&&snapshot.seq<visible_.seq)return false;
  job_=snapshot;active_=true;invalid_=pixels_=false;return true;
 }
 // Last-confirmed boot-bound K1 epochs are supplied by caller. A missing K1
 // for a newer heartbeat does not zero them. First confirmation or any epoch
 // change invalidates an unconfirmed/older job; bare legacy epochs may stay0.
 bool incompatible(const RenderIdentity& live,uint32_t live_seq)const{
  return active_&&(!live.valid()||!(job_.identity==live)||(live.linked&&live_seq<job_.seq));
 }
 // Device::valid does NOT itself reject shown==0. Every loss of a completed
 // shown identity invokes the required hook BEFORE clearing it.
 bool observe_loses_authority(const RenderIdentity& live,uint32_t seq)const{return shown()&&(retired_display(live,seq)||(incompatible(live,seq)&&pixels_));}
 bool begin_loses_authority()const{return active_&&!invalid_&&shown()&&!(job_.identity==visible_.identity);}
 void invalidate(){
  if(!active_)return;
  invalid_=true;
  if(pixels_)lose_authority();
 }
 void observe(const RenderIdentity& live,uint32_t live_seq){
  if(retired_display(live,live_seq))lose_authority(); // Also when there is no active job.
  if(incompatible(live,live_seq))invalidate();
 }
 bool begin_pixels(const RenderIdentity& live,uint32_t live_seq){
  observe(live,live_seq);
  if(!active_||invalid_)return false;
  pixels_=true;
  if(!coherent_||!(job_.identity==visible_.identity))lose_authority();
  if(job_.preview)transfer_ambiguous_=true;
  return true;
 }
 bool touch_allowed(const RenderIdentity& live)const{return shown()&&live.valid()&&visible_.identity==live;}
 bool local_touch_allowed(const RenderIdentity& live)const{return coherent_&&live.valid()&&visible_.identity==live&&!visible_.preview&&!transfer_ambiguous_;}
 // A clean old raster may still address OLD host-validated ACK/voice state
 // while a new selection's image is prepared, as in the original loop. Touch
 // needs full live identity equality. Never carry control across a link/boot.
 bool controls_blocked(const RenderIdentity& live)const{return !shown()||!live.valid()||!live.linked||visible_.identity.boot!=live.boot||visible_.identity.source!=live.source||visible_.identity.generation!=live.generation||visible_.identity.view!=live.view||visible_.preview||transfer_ambiguous_;}
 bool publish(const RenderIdentity& live,uint32_t live_seq,bool transfer_complete){
  observe(live,live_seq);
  if(!active_||invalid_||!pixels_||!transfer_complete)return false;
  visible_=job_;coherent_=true;active_=false;transfer_ambiguous_=false;return true;
 }
 // Only close a discarded job once submitted DMA is drained; caller additionally
 // requires its transfer/resource controller healthy before starting recovery.
 bool retire(bool drained){
  if(!active_||!invalid_||!drained)return false;
  active_=false;return true;
 }
 // Snapshot-only cleanup: never clear a newer mid-job event merely because the
 // live W1 has already ACKed it. Its own resulting image is not yet published.
 bool clears_paging(uint32_t pending)const{return !active_&&!invalid_&&coherent_&&visible_.identity.linked&&!visible_.local&&pending&&visible_.paging_event==pending&&visible_.event_ack>=pending;}
};
} // namespace watcher_render
