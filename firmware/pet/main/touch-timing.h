#pragma once
#include "wire.h"
#include <stdint.h>
#include <cstdio>
namespace watcher_touch {
// Diagnostic-only fixed RAM; no control state, I/O, coordinates or identities.
// Extended schema:48..95 gate/ACK/paint;96..127 slow operations;
//128..134 latest release rejection;144..158 last slow-loop breakdown.
// All counters saturate.32-bit times wrap modulo2^32; ages/durations saturate.
class Timing {
 uint16_t values_[192]{},costs_[7]{};
 int64_t loop_=-1,read_=-1,blocked_=-1,metadata_=-1,issued_=-1,ack_=-1,gate_at_=-1,paint_at_=-1;
 uint32_t pending_=0,target_=0,paint_ack_=0;unsigned gate_mask_=0;bool read_in_loop_=false,paint_in_loop_=false;
 static uint32_t elapsed32(int64_t start,int64_t end){if(start<0||end<start)return 0;const auto n=static_cast<uint64_t>(end-start);return n>UINT32_MAX?UINT32_MAX:static_cast<uint32_t>(n);}
 static uint16_t elapsed(int64_t start,int64_t end){const auto n=elapsed32(start,end);return n>65535?65535:static_cast<uint16_t>(n);}
 static void store32(uint16_t* out,unsigned i,uint32_t n){out[i]=n&65535;out[i+1]=n>>16;}
 uint32_t value32(unsigned i)const{return values_[i]|(static_cast<uint32_t>(values_[i+1])<<16);}
 void add32(unsigned i,uint32_t n){const auto before=value32(i);store32(values_,i,n>UINT32_MAX-before?UINT32_MAX:before+n);}
 void maximum(unsigned i,uint16_t value){if(value>values_[i])values_[i]=value;}
 void increment(unsigned i){if(values_[i]!=65535)++values_[i];}
 bool interval(int64_t begin,int64_t end){if(begin<0||end<begin){increment(95);return false;}return true;}
 // Channels:buttons,audio,IMU,ingress,voice,touch-read,render-attempt,loop.
 // Named operations >20ms; rendering >80ms; complete loop >100ms.
 void slow(unsigned channel,int64_t begin,int64_t end,unsigned threshold){
  if(static_cast<uint64_t>(end-begin)<=threshold)return;
  const unsigned i=96+4*channel;increment(i);values_[i+1]=elapsed(begin,end);store32(values_,i+2,static_cast<uint32_t>(end));
 }
 public:
 void loop(int64_t now){
  if(now<0){increment(95);return;}
  store32(values_,0,static_cast<uint32_t>(now));
  if(loop_>=0&&interval(loop_,now)){
   values_[2]=elapsed(loop_,now);maximum(3,values_[2]);slow(7,loop_,now,100);
   if(now-loop_>100){
    increment(4);increment(144);values_[145]=values_[2];store32(values_,146,static_cast<uint32_t>(now));
    uint32_t accounted=0;for(unsigned i=0;i<7;i++){values_[148+i]=costs_[i];accounted+=costs_[i];}
    const auto total=elapsed32(loop_,now);const auto other=total>accounted?total-accounted:0;
    values_[155]=other>65535?65535:static_cast<uint16_t>(other);values_[156]=gate_mask_;values_[157]=read_in_loop_;values_[158]=paint_in_loop_;
   }
  }
  loop_=now;for(auto& n:costs_)n=0;read_in_loop_=paint_in_loop_=false;
 }
 void render(int64_t begin,int64_t end){if(!interval(begin,end))return;values_[5]=elapsed(begin,end);costs_[6]=values_[5];maximum(6,values_[5]);if(end-begin>80)increment(7);increment(47);slow(6,begin,end,80);}
 void read(int64_t begin,int64_t end){
  if(!interval(begin,end))return;
  costs_[5]=elapsed(begin,end);read_in_loop_=true;maximum(8,costs_[5]);slow(5,begin,end,20);
  if(read_>=0&&interval(read_,begin)){maximum(9,elapsed(read_,begin));if(begin-read_>100)increment(10);}
  read_=begin;
 }
 // Observe elapsed time against the PREVIOUS mask. Causes overlap; their sum
 // is NOT total blocked time. No reconstruction of intra-loop transitions.
 void gate(int64_t now,bool eligible,unsigned blocked,bool metadata_blocked=false){
  values_[18]=blocked&255;
  if(now<0){increment(95);return;}
  unsigned mask=eligible?0u:(blocked&255u);
  if(!eligible&&metadata_blocked)mask=(mask&~128u)|256u;
  if(!eligible&&!mask)mask=128;
  if(gate_at_>=0){
   if(now<gate_at_){increment(95);gate_mask_=0;blocked_=-1;}
   else if(gate_mask_){const auto dt=elapsed32(gate_at_,now);add32(49,dt);for(unsigned i=0;i<9;i++)if(gate_mask_&(1u<<i))add32(54+3*i,dt);}
  }
  for(unsigned i=0;i<9;i++)if((mask&(1u<<i))&&!(gate_mask_&(1u<<i)))increment(53+3*i);
  if(mask){
   if(!gate_mask_){increment(48);blocked_=now;values_[19]=0;values_[52]=0;}
   values_[19]|=blocked&255;values_[52]|=mask;
   values_[46]=elapsed(blocked_,now);maximum(20,values_[46]);
  }else if(blocked_>=0){values_[46]=elapsed(blocked_,now);maximum(20,values_[46]);blocked_=-1;}
  gate_at_=now;gate_mask_=mask;values_[51]=mask;
 }
 void issue(uint32_t id,int64_t now){
  if(!id||now<0)return;
  if(pending_){increment(86);}
  if(ack_>=0){increment(88);}
  pending_=id;issued_=now;ack_=-1;paint_ack_=0;increment(29);
 }
 void ack(uint32_t id,uint32_t selected,int64_t now){
  if(!pending_||id<pending_||issued_<0||now<issued_)return;
  values_[30]=elapsed(issued_,now);maximum(31,values_[30]);paint_ack_=pending_;pending_=0;issued_=-1;ack_=now;target_=selected;increment(85);
 }
 // Full-frame application publication: legacy push return, bounded renderer's
 // completed bus transfer. Neither establishes physical scanout completion.
 // Delayed scenes must cover the measured ACK, not merely share its selection.
 void paint(uint32_t selected,int64_t now,bool linked=true,uint32_t covered_ack=UINT32_MAX){
  if(now<0){increment(95);return;}
  add32(89,1);paint_at_=now;store32(values_,91,static_cast<uint32_t>(now));paint_in_loop_=true;
  if(!linked||ack_<0||selected!=target_||now<ack_||covered_ack<paint_ack_)return;
  values_[32]=elapsed(ack_,now);maximum(33,values_[32]);ack_=-1;increment(87);
 }
 void metadata(bool waiting,int64_t now){
  if(now<0)return;
  if(waiting){if(metadata_<0||now<metadata_){metadata_=now;increment(34);}maximum(35,elapsed(metadata_,now));}
  else if(metadata_>=0){maximum(35,elapsed(metadata_,now));metadata_=-1;}
 }
 void denied(){increment(36);}
 void frame_age(int64_t age){if(age>=0)maximum(37,age>65535?65535:static_cast<uint16_t>(age));}
 // Slots38..42:buttons,audio,IMU,ingress,voice. Reject misuse, no OOB.
 void phase(unsigned slot,int64_t begin,int64_t end){if(slot<38||slot>42||!interval(begin,end))return;costs_[slot-38]=elapsed(begin,end);maximum(slot,costs_[slot-38]);slow(slot-38,begin,end,20);}
 // Retired means unresolved, not successful (link loss or supersession).
 void retire(){
  if(pending_){increment(86);}
  if(ack_>=0){increment(88);}
  pending_=paint_ack_=0;issued_=ack_=-1;
 }
 template<class Sampler,class Swipe>
 void snapshot(uint16_t (&out)[48],const Sampler& sampler,const Swipe& swipe,bool eligible)const{
  for(unsigned i=0;i<48;i++)out[i]=values_[i];
  const auto& s=sampler.stats();const auto& c=swipe.cancellation_stats();
  out[11]=s.reads;out[12]=s.ioErrors;out[13]=s.invalid;out[14]=s.contacts;out[15]=s.releases;out[16]=swipe.emitted();
  out[17]=(swipe.armed()?1:0)|(swipe.tracking()?2:0)|(eligible?4:0)|(sampler.ready()?8:0)|(sampler.halted()?16:0);
  for(unsigned i=0;i<8;i++)out[21+i]=c.counts[i];
  out[43]=c.reason|(c.release_flags<<8);out[44]=c.at&65535;out[45]=c.at>>16;
 }
 template<class Sampler,class Swipe>
 void snapshot_full(uint16_t (&out)[192],const Sampler& sampler,const Swipe& swipe,bool eligible,int64_t now)const{
  uint16_t core[48];snapshot(core,sampler,swipe,eligible);
  for(unsigned i=0;i<192;i++)out[i]=i<48?core[i]:values_[i];
  store32(out,0,static_cast<uint32_t>(now>=0?now:0));
  out[80]=(pending_?1:0)|(ack_>=0?2:0)|(paint_at_>=0?4:0);
  store32(out,81,pending_?elapsed32(issued_,now):0);store32(out,83,ack_>=0?elapsed32(ack_,now):0);
  store32(out,93,elapsed32(paint_at_,now));
  const auto& c=swipe.cancellation_stats();out[128]=c.last_release_flags;store32(out,129,c.last_release_at);
  for(unsigned i=0;i<4;i++)out[131+i]=c.release_counts[i];
  const auto& s=sampler.stats();out[160]=sampler.halted()?9:sampler.stage();out[161]=s.setupFailures;out[162]=s.invalidCount;out[163]=s.invalidX;out[164]=s.invalidY;out[165]=s.gapMs;out[166]=s.maxGapMs;
 }
};
static_assert(sizeof(Timing)<=640,"Bounded diagnostic RAM");
// The pointer always names48 values; callers below constrain page/extent.
inline size_t timing_values_line(char (&line)[192],uint32_t boot,uint32_t request,uint32_t seq,uint32_t rendered,const uint16_t* values,unsigned page=4){
 const int head=page==4?
  std::snprintf(line,sizeof line,"TD3 %lu %lu %lu %lu ",static_cast<unsigned long>(boot),static_cast<unsigned long>(request),static_cast<unsigned long>(seq),static_cast<unsigned long>(rendered)):
  std::snprintf(line,sizeof line,"TD4 %lu %lu %lu %lu %u ",static_cast<unsigned long>(boot),static_cast<unsigned long>(request),static_cast<unsigned long>(seq),static_cast<unsigned long>(rendered),page);
 if(head<0||static_cast<size_t>(head)+140>=sizeof line)return 0;
 static constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
 const auto byte=[&](unsigned i){return static_cast<uint8_t>(values[i/2]>>((i%2)*8));};
 size_t used=head;
 for(unsigned i=0;i<96;i+=3){const uint32_t v=(static_cast<uint32_t>(byte(i))<<16)|(static_cast<uint32_t>(byte(i+1))<<8)|byte(i+2);for(int shift=18;shift>=0;shift-=6)line[used++]=alphabet[(v>>shift)&63];}
 line[used++]=' ';
 const int tail=std::snprintf(line+used,sizeof(line)-used,"%lu\n",static_cast<unsigned long>(watcher_wire::crc(line,used)));
 return tail>0&&static_cast<size_t>(tail)<sizeof(line)-used?used+tail:0;
}
inline size_t timing_line(char (&line)[192],uint32_t boot,uint32_t request,uint32_t seq,uint32_t rendered,const uint16_t (&values)[48]){return timing_values_line(line,boot,request,seq,rendered,values);}
inline size_t timing_full_lines(char (&burst)[768],uint32_t boot,uint32_t request,uint32_t seq,uint32_t rendered,const uint16_t (&values)[192]){
 char line[192];size_t used=0;
 for(unsigned page=0;page<4;page++){
  const auto n=timing_values_line(line,boot,request,seq,rendered,values+48*page,page);
  if(!n||n>sizeof(burst)-used)return 0;
  for(size_t i=0;i<n;i++)burst[used++]=line[i];
 }
 return used;
}
} // namespace watcher_touch
