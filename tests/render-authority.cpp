#include "../firmware/render/render-authority.h"
#include "../firmware/render/transfer-job.h"
#include "../firmware/pet/main/swipe.h"
#include "../firmware/pet/main/ghost-wire.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
using namespace watcher_render;
struct TestAuthority : RenderAuthority {TestAuthority():RenderAuthority(+[](void*) noexcept {},nullptr){}};
static RenderIdentity identity(){return {7,1,1,0,3,0,5,6,true};}
static void complete(RenderAuthority& a,const RenderIdentity& k,uint32_t seq,bool preview=false,uint32_t event=0,uint32_t ack=0){
 assert(a.start({k,seq,event,ack,preview}));assert(a.begin_pixels(k,seq));assert(!a.publish(k,seq,false));assert(a.publish(k,seq,true));
}
int main(){
 const auto k=identity();TestAuthority a;assert(!a.shown()&&!a.touch_allowed(k)&&a.controls_blocked(k));
 complete(a,k,10);assert(a.shown()==10&&a.touch_allowed(k)&&!a.controls_blocked(k));
 assert(a.start({k,11,0,0,false}));assert(a.begin_pixels(k,11));
 for(uint32_t seq=12;seq<=61;++seq){a.observe(k,seq);assert(!a.invalid()&&a.shown()==10&&a.touch_allowed(k)&&!a.controls_blocked(k));}
 assert(!a.publish(k,61,false));assert(a.publish(k,61,true));assert(a.shown()==11); // NOT live61.
 assert(!a.start({k,10,0,0,false})); // Never roll back completion in the same boot.
 assert(a.start({k,62,0,0,true}));assert(!a.controls_blocked(k));assert(a.begin_pixels(k,62));assert(a.controls_blocked(k)&&a.touch_allowed(k));
 assert(a.publish(k,62,true)&&a.controls_blocked(k));complete(a,k,63);assert(!a.controls_blocked(k));
 // Every identity seam invalidates ANY job, even one originally same-identity.
 for(unsigned field=0;field<9;++field){
  TestAuthority b;complete(b,k,10);assert(b.start({k,11,0,0,false}));assert(b.begin_pixels(k,11));auto changed=k;
  switch(field){case 0:++changed.boot;break;case 1:changed.source=2;break;case 2:++changed.generation;break;case 3:changed.selected=1;break;case 4:changed.count=4;break;case 5:changed.rotation=1;break;case 6:++changed.roster;break;case 7:++changed.selection;break;default:changed.linked=false;break;}
  assert(b.observe_loses_authority(changed,12));b.observe(changed,12);assert(b.invalid()&&!b.shown()&&b.controls_blocked(changed));
  b.observe(k,13);assert(b.invalid());assert(!b.publish(k,13,true));assert(!b.retire(false));assert(b.retire(true));
  assert(b.start({k,14,0,0,false}));assert(!b.shown()&&b.controls_blocked(k));assert(b.begin_pixels(k,14)&&b.publish(k,14,true));assert(b.shown()==14);
 }
 // Completed authority is retired on boot/link loss even with NO active job.
 for(unsigned field=0;field<4;++field){TestAuthority idle;complete(idle,k,10);auto retired=k;if(field==0)++retired.boot;else if(field==1)++retired.source;else if(field==2)++retired.generation;else retired.linked=false;assert(idle.observe_loses_authority(retired,11));idle.observe(retired,11);assert(!idle.shown()&&idle.controls_blocked(retired));}
 // Aborted RAM-only work leaves the completed identity intact, but live tuple
 // changes still suppress controls. A drained fresh recovery can then publish.
 TestAuthority clean;complete(clean,k,10);assert(clean.start({k,11,0,0,false}));auto next=k;next.selected=1;++next.selection;
 assert(!clean.observe_loses_authority(next,12));clean.observe(next,12);assert(clean.invalid()&&clean.shown()==10&&!clean.touch_allowed(next)&&!clean.controls_blocked(next));assert(clean.retire(true));
 assert(clean.start({next,12,0,0,false})&&clean.begin_loses_authority());assert(clean.begin_pixels(next,12));assert(!clean.shown());assert(clean.publish(next,12,true)&&clean.shown()==12);
 // Snapshot-only event cleanup: live ACK of newerP2 cannot make oldP1 image
 // clearP2. A completed snapshot containingP2's own ACK is necessary.
 TestAuthority events;complete(events,k,10,false,1,1);assert(events.clears_paging(1)&&!events.clears_paging(2));
 assert(events.start({k,11,1,1,false}));assert(events.begin_pixels(k,12));assert(events.publish(k,20,true));assert(!events.clears_paging(2));complete(events,k,21,false,2,2);assert(events.clears_paging(2));
 // Actual Swipe ignores heartbeat/shown-seq churn but preserves origin.shown.
 watcher_touch::Swipe swipe;watcher_touch::Context c{true,7,10,0,3,0,1,1,5,6};
 swipe.update({watcher_touch::Kind::released,0,0},c,0);swipe.update({watcher_touch::Kind::released,0,0},c,20);
 swipe.update({watcher_touch::Kind::contact,10,100},c,40);c.shown=11;swipe.update({watcher_touch::Kind::contact,60,100},c,80);c.shown=12;swipe.update({watcher_touch::Kind::contact,110,100},c,120);
 swipe.update({watcher_touch::Kind::released,0,0},c,140);const auto event=swipe.update({watcher_touch::Kind::released,0,0},c,160);assert(event.kind&&event.shown==10);
 // Actual K1 contract is seq-exact. Missing a newer heartbeat's K1 retains
 // confirmed epochs, whereas accepting changed epochs invalidates the job.
 watcher_ghost::FocusContext focus;watcher_wire::Frame frame{};frame.boot=7;frame.seq=10;
 auto accept=[&](unsigned seq,unsigned roster,unsigned selection){frame.seq=seq;std::string line="K1 7 "+std::to_string(seq)+" "+std::to_string(roster)+" "+std::to_string(selection)+" ";line+=std::to_string(watcher_wire::crc(line.data(),line.size()));return focus.accept(line.data(),line.size(),frame);};
 assert(accept(10,5,6)&&focus.matches(frame));frame.seq=11;assert(!focus.matches(frame)&&focus.roster==5&&focus.selection==6);
 TestAuthority missing;complete(missing,k,10);assert(missing.start({k,11,0,0,false}));missing.observe(k,11);assert(!missing.invalid());
 assert(accept(12,7,8));auto revised=k;revised.roster=focus.roster;revised.selection=focus.selection;missing.observe(revised,12);assert(missing.invalid());assert(!accept(11,5,6));
 // Compose real authority/transfer controllers: queue rotation, drain pending
 // DMA, then apply rotation and fully repaint. No geometry change in flight.
 struct TransferAdapter {
  bool busy=false,allow_finish=false;unsigned pushes=0;
  bool ready_for_band(){return !busy;}
  bool submit_band(const TransferBand&){assert(!busy);busy=true;++pushes;return true;}
  bool finish_band(){if(!allow_finish)return false;busy=false;return true;}
 } hardware;
 TestAuthority rotated;TransferJob transfer;complete(rotated,k,10);assert(rotated.start({k,11,0,0,false}));assert(rotated.begin_pixels(k,11));assert(transfer.begin(0,16));transfer.step(hardware);assert(transfer.pending());
 auto turned=k;turned.rotation=1;unsigned applied_rotation=0;
 assert(rotated.observe_loses_authority(turned,12));rotated.observe(turned,12);transfer.abort();
 assert(!rotated.retire(!transfer.pending())&&applied_rotation==0&&!rotated.shown());
 transfer.step(hardware);assert(transfer.pending()&&hardware.pushes==1); // No rotation/submission while busy.
 hardware.allow_finish=true;transfer.step(hardware);assert(!transfer.pending()&&!hardware.busy);assert(rotated.retire(true));
 applied_rotation=1;assert(applied_rotation==turned.rotation);assert(transfer.begin(applied_rotation,16));assert(rotated.start({turned,12,0,0,false}));assert(rotated.begin_pixels(turned,12));
 while(!transfer.publishable()){transfer.step(hardware);if(!transfer.publishable())assert(!rotated.publish(turned,13,false));}
 assert(rotated.publish(turned,13,transfer.publishable())&&rotated.shown()==12);
 // Required hook runs exactly once per loss, while old shown remains usable.
 struct Loss {RenderAuthority* owner=nullptr;unsigned calls=0;} loss;
 RenderAuthority hooked(+[](void* pointer) noexcept {auto& value=*static_cast<Loss*>(pointer);assert(value.owner->shown()==10);++value.calls;},&loss);loss.owner=&hooked;
 complete(hooked,k,10);assert(hooked.start({k,11,0,0,false}));assert(hooked.begin_pixels(k,11));hooked.observe(next,12);assert(loss.calls==1&&!hooked.shown());hooked.observe(k,13);hooked.invalidate();assert(loss.calls==1);
 RenderAuthority no_hook(nullptr,nullptr);assert(!no_hook.start({k,1,0,0,false}));
 // Invalid fields and future-seq snapshots never publish.
 TestAuthority bad;auto invalid=k;invalid.rotation=4;assert(!bad.start({invalid,1,0,0,false}));assert(!bad.start({k,0,0,0,false}));assert(bad.start({k,20,0,0,false}));assert(!bad.begin_pixels(k,19)&&bad.invalid());
 auto offline=k;offline.linked=false;TestAuthority disconnected;complete(disconnected,offline,0);assert(disconnected.coherent()&&!disconnected.shown()&&!disconnected.touch_allowed(offline));
 // Local Settings publication is usable offline but never creates session authority.
 TestAuthority local;auto local_key=k;local_key.view=1;
 assert(local.start({local_key,10,0,0,false,true}));assert(local.begin_pixels(local_key,10));assert(local.publish(local_key,10,true));
 assert(local.local_touch_allowed(local_key)&&!local.shown()&&!local.touch_allowed(local_key)&&local.controls_blocked(local_key));
 auto newer_local=local_key;++newer_local.view;assert(!local.local_touch_allowed(newer_local));
 assert(local.start({newer_local,11,0,0,false,true}));assert(local.begin_pixels(newer_local,11));assert(!local.local_touch_allowed(local_key));assert(local.publish(newer_local,11,true));
 auto offline_local=newer_local;offline_local.linked=false;local.observe(offline_local,0);assert(!local.local_touch_allowed(offline_local));
 assert(local.start({offline_local,0,0,0,false,true})&&local.begin_pixels(offline_local,0)&&local.publish(offline_local,0,true));
 assert(local.local_touch_allowed(offline_local)&&!local.shown());
 TestAuthority entry;complete(entry,k,10);auto entering=k;++entering.view;assert(entry.controls_blocked(entering)&&!entry.local_touch_allowed(entering));
 std::cout<<"render authority: heartbeat, preview,9 identity seams, ABA, DMA retirement, paging, real Swipe/K1 and mandatory pre-loss hook contracts pass\n";
}
