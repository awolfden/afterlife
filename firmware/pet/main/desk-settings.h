#pragma once
#include "wire.h"
#include <cstring>
#include <cstdio>
namespace watcher_settings {
struct Values {
 uint32_t flags=5,brightness=160;
 bool valid()const{return flags<=15&&(brightness==80||brightness==160||brightness==240);}
 bool operator==(const Values& b)const{return flags==b.flags&&brightness==b.brightness;}
 bool operator!=(const Values& b)const{return !(*this==b);}
 bool vibration()const{return flags&1;}bool sounds()const{return flags&2;}bool animations()const{return flags&4;}bool cycling()const{return flags&8;}
};
struct Echo {uint32_t boot=0,seq=0,revision=0,ack=0,status=0,forced=0;char token[33]{};Values values;};
inline bool number(const char*& p,const char* end,uint32_t& out){const auto* start=p;return watcher_wire::number(p,end,out)&&(p-start==1||*start!='0');}
inline bool space(const char*& p,const char* end){return p!=end&&*p++==' ';}
inline bool parse(const char* line,size_t size,Echo& out){
 if(!line||size<4||size>=192||std::memcmp(line,"S1 ",3))return false;
 Echo value;const char* p=line+3;const char* end=line+size;
 if(!number(p,end,value.boot)||!value.boot||!space(p,end)||!number(p,end,value.seq)||!value.seq||!space(p,end)||end-p<33)return false;
 for(unsigned i=0;i<32;++i){const char c=*p++;if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;value.token[i]=c;}
 if(!space(p,end))return false;
 uint32_t* fields[]={&value.revision,&value.ack,&value.status,&value.values.flags,&value.values.brightness,&value.forced};
 for(auto* field:fields)if(!number(p,end,*field)||!space(p,end))return false;
 const char* checksum=p;uint32_t sum=0;
 if(!number(p,end,sum)||p!=end||watcher_wire::crc(line,static_cast<size_t>(checksum-line))!=sum||!value.revision||value.status>1||value.forced>1||!value.values.valid())return false;
 out=value;return true;
}
inline size_t finish(char (&out)[193],int size){
 if(size<1||size>180)return 0;
 const int tail=std::snprintf(out+size,sizeof(out)-size,"%lu\n",static_cast<unsigned long>(watcher_wire::crc(out,size)));
 return tail>0&&size+tail<=192?static_cast<size_t>(size+tail):0;
}
enum class Save {temporary,saving,saved,unsaved};
class Sync {
 Values values_{},sent_{};char token_[33]{},retired_[64][33]{};unsigned retired_count_=0;
 bool remember(){if(!token_[0])return true;if(retired_count_==64)return false;std::memcpy(retired_[retired_count_++],token_,sizeof token_);return true;}
 uint32_t boot_=0,seq_=0,revision_=0,ack_=0,pending_=0,base_=0;
 int64_t now_=-1,started_=0,last_send_=-1000;
 bool live_=false,dirty_=false,failed_=false,forced_=false,paused_=false;
 Save save_=Save::temporary;
 public:
 const Values& values()const{return values_;}Save save()const{return save_;}
 bool supported()const{return token_[0]!=0&&live_;}bool forced()const{return forced_;}
 bool animations()const{return values_.animations()&&!forced_;}
 uint32_t revision()const{return revision_;}
 void retire(){remember();token_[0]=0;revision_=ack_=pending_=base_=0;live_=dirty_=failed_=forced_=paused_=false;save_=Save::temporary;last_send_=-1000;}
 void context(uint32_t boot,uint32_t seq,int64_t now,bool live){
  if(now<0||now<now_){retire();return;}
  if(boot!=boot_){retire();retired_count_=0;}else if(!live&&live_)retire();
  // W1 acceptance is monotonic within a boot. Old-frame tokens cannot match
  // a later frame; retain same-frame replay fences only until it advances.
  if(boot==boot_&&seq<seq_) {retire();return;}
  if(seq!=seq_)retired_count_=0;
  boot_=boot;seq_=seq;now_=now;live_=live&&boot&&seq;
  if(pending_&&!paused_&&now-started_>=1500){paused_=true;save_=Save::unsaved;}
 }
 bool change(Values next){
  if(!next.valid()||next==values_)return false;
  values_=next;dirty_=true;
  // Explicit new input may retry the SAME outstanding ID/payload, then send
  // the coalesced desired value. Never reuse an ambiguous ID with new data.
  if(paused_&&!failed_){paused_=false;started_=now_;}
  save_=supported()?(failed_?Save::unsaved:Save::saving):Save::temporary;return true;
 }
 bool receive(const char* line,size_t size){
  Echo e;if(!live_||!parse(line,size,e)||e.boot!=boot_||e.seq!=seq_)return false;
  if(std::strcmp(e.token,token_)!=0){
   if((token_[0]&&e.ack)||retired_count_==64)return false;
   for(unsigned i=0;i<retired_count_;++i)if(!std::strcmp(e.token,retired_[i]))return false;
   if(!remember())return false; // Same-frame tombstones are never evicted.
   std::memcpy(token_,e.token,sizeof token_);revision_=e.revision;ack_=e.ack;pending_=0;dirty_=paused_=false;failed_=e.status!=0;
   values_=e.values;forced_=e.forced;save_=failed_?Save::unsaved:Save::saved;last_send_=-1000;return true;
  }
  if(e.revision<revision_||e.ack<ack_||failed_)return false;
  if(pending_){
   if(e.ack!=pending_)return false;
   if(e.status){pending_=0;failed_=true;save_=Save::unsaved;return true;}
   if(e.values!=sent_||e.revision<base_)return false;
   revision_=e.revision;ack_=e.ack;pending_=0;paused_=false;forced_=e.forced;
   dirty_=values_!=sent_;save_=dirty_?Save::saving:Save::saved;return true;
  }
  if(e.ack!=ack_)return false;
  if(e.status){failed_=true;save_=Save::unsaved;return true;}
  revision_=e.revision;forced_=e.forced;
  if(!dirty_){values_=e.values;save_=Save::saved;}return true;
 }
 size_t advertisement(char (&out)[193])const{
  if(!live_)return 0;
  return finish(out,std::snprintf(out,sizeof out,"S0 %lu %lu 1 ",(unsigned long)boot_,(unsigned long)seq_));
 }
 size_t edit(char (&out)[193]){
  if(!supported()||failed_||paused_||(!dirty_&&!pending_)||now_-last_send_<250)return 0;
  if(!pending_){
   if(ack_==UINT32_MAX){failed_=true;save_=Save::unsaved;return 0;}
   pending_=ack_+1;base_=revision_;sent_=values_;started_=now_;
  }
  last_send_=now_;
  return finish(out,std::snprintf(out,sizeof out,"S2 %lu %lu %s %lu %lu %lu %lu ",(unsigned long)boot_,(unsigned long)seq_,token_,(unsigned long)pending_,(unsigned long)base_,(unsigned long)sent_.flags,(unsigned long)sent_.brightness));
 }
 size_t interaction(char (&out)[193],uint32_t id,unsigned activity)const{
  if(!supported()||!id||activity>7)return 0;
  return finish(out,std::snprintf(out,sizeof out,"S3 %lu %lu %s %lu %u ",(unsigned long)boot_,(unsigned long)seq_,token_,(unsigned long)id,activity));
 }
};
} // namespace watcher_settings
