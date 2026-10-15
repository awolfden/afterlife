#pragma once
#include "desk-settings.h"
#include "settings-input.h"
#include "settings-scroll.h"
namespace watcher_settings {
// Single-loop glue. No display/transport IO, ownership changes, or persistence.
class Runtime {
 Sync sync_;Input input_;Scroll scroll_;Values committed_{};bool animate_=true;
 bool open_=false,healthy_=true;unsigned top_=0;uint32_t generation_=1,interaction_=1;
 int64_t now_=-1,last_interaction_=-10000,last_status_=-1000;unsigned activity_=0;
 void changed(){if(generation_==UINT32_MAX)healthy_=false;else ++generation_;}
 void witness(){const unsigned next=(open_?1u:0u)|(input_.contact()?2u:0u)|((now_<last_interaction_||now_-last_interaction_<1000)?4u:0u);if(next!=activity_){if(interaction_==UINT32_MAX)healthy_=false;else ++interaction_;activity_=next;}}
 public:
 const Sync& sync()const{return sync_;}bool open()const{return open_;}unsigned top()const{return top_;}
 uint32_t generation()const{return generation_;}bool healthy()const{return healthy_;}
 unsigned activity()const{return activity_;}
 bool scrolling()const{return open_&&(input_.dragging()||scroll_.moving());}
 int scroll_offset(bool host_reduced){animate_=animations(host_reduced);return scroll_.update(now_,top_,input_.dragging(),input_.displacement(),animate_);}
 unsigned outputs()const{
  if(!healthy_||!sync_.supported())return 0;
  const unsigned allowed=committed_.flags&sync_.values().flags;
  return ((allowed&1)?2u:0u)|((allowed&2)?1u:0u);
 }
 bool animations(bool host_reduced)const{return !host_reduced&&sync_.animations();}
 void context(uint32_t boot,uint32_t seq,int64_t now,bool live){
  const auto before=sync_.save();const bool forced=sync_.forced(),supported=sync_.supported();
  if(now<now_){input_.cancel();scroll_.reset(top_);last_interaction_=now_;}now_=now;
  sync_.context(boot,seq,now,live);
  if(before!=sync_.save()||forced!=sync_.forced()||supported!=sync_.supported())changed();
  witness();
 }
 bool receive(const char* line,size_t length){
  const auto before=sync_.values();const auto save=sync_.save();const bool forced=sync_.forced(),supported=sync_.supported();
  if(!sync_.receive(line,length))return false;
  Echo echo;if(!parse(line,length,echo))return false;committed_=echo.values;
  if(before!=sync_.values()||save!=sync_.save()||forced!=sync_.forced()||supported!=sync_.supported()){input_.cancel();scroll_.reset(top_);changed();}
  return true;
 }
 bool change(unsigned row){
  if(!healthy_||row>4)return false;
  auto next=sync_.values();if(row<4)next.flags^=1u<<row;else next.brightness=next.brightness==80?160:next.brightness==160?240:80;
  if(!sync_.change(next))return false;
  changed();last_interaction_=now_;witness();return true;
 }
 void cancel(){input_.cancel();scroll_.reset(top_);last_interaction_=now_;witness();}
 bool back(){if(!open_)return false;open_=false;input_.cancel();scroll_.reset(top_);changed();last_interaction_=now_;witness();return true;}
 InputEvent touch(watcher_touch::Sample sample,bool published,unsigned rotation){
  const bool was_contact=input_.contact();
  const auto event=input_.update(sample,{healthy_&&published&&(!scroll_.moving()||input_.contact()),open_,generation_,rotation,top_},now_);
  if(was_contact||sample.kind!=watcher_touch::Kind::released)last_interaction_=now_;
  switch(event.action){
   case Action::open:open_=true;top_=0;scroll_.reset(top_);changed();last_interaction_=now_;break;
   case Action::back:back();break;
   case Action::toggle:change(event.row);break;
   case Action::scroll:{const unsigned next=event.direction>0?(top_<2?top_+1:2):(top_?top_-1:0);if(next!=top_){top_=next;changed();}last_interaction_=now_;break;}
   default:break;
  }
  scroll_.update(now_,top_,input_.dragging(),input_.displacement(),animate_&&animations(false));
  witness();return event;
 }
 // Caller-owned scratch avoids a multi-kilobyte loop-stack temporary.
 struct Emission {Sync next;char bytes[769]{};};
 template<class Admit> bool status_batch(const char* d2,size_t length,Emission& scratch,Admit admit){
  if(!d2||length<4||length>192||std::memcmp(d2,"D2 ",3)||d2[length-1]!='\n')return false;
  for(size_t i=0;i+1<length;++i)if(d2[i]=='\n')return false;
  std::memcpy(scratch.bytes,d2,length);scratch.next=sync_;size_t used=length;
  if(healthy_&&now_>=0&&now_-last_status_>=250){
   char line[193];const auto append=[&](size_t size){if(size){std::memcpy(scratch.bytes+used,line,size);used+=size;}};
   append(scratch.next.advertisement(line));append(scratch.next.edit(line));append(scratch.next.interaction(line,interaction_,activity_));
  }
  scratch.bytes[used]=0;
  // The callback must be all-or-none, synchronous and nonreentrant. Admission
  // is not delivery: successful enqueues still retain normal bounded retries.
  if(!admit(scratch.bytes,used))return false;
  if(used>length){sync_=scratch.next;last_status_=now_;}return true;
 }
};
} // namespace watcher_settings
