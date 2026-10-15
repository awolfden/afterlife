// Counter assertions intentionally exercise the opt-in diagnostic build.
#define WATCHER_TOUCH_DIAGNOSTICS 1
#include "../firmware/pet/main/touch.h"
#include "../firmware/pet/main/touch-diagnostic.h"
#include <cassert>
#include <cstring>
#include <vector>
#include <utility>
using namespace watcher_touch;
static Context context(unsigned rotation=0){return {true,123,40,0,3,rotation,1,0};}
static Sample finger(int x,int y,unsigned rotation){return orient({Kind::contact,x,y},(4-rotation)%4);}
static void arm(Swipe& s,const Context& c){assert(!s.update({Kind::released},c,0).kind);assert(!s.update({Kind::released},c,20).kind);}
static Event run(unsigned rotation,bool left,int vertical=0){
 Swipe s;auto c=context(rotation);arm(s,c);const int start=left?180:50,d=left?-1:1;
 for(unsigned i=0;i<4;i++)assert(!s.update(finger(start+d*static_cast<int>(i)*30,100+vertical*static_cast<int>(i),rotation),c,40+i*40).kind);
 assert(!s.update({Kind::released},c,180).kind);auto event=s.update({Kind::released},c,200);
 assert(!s.update({Kind::released},c,220).kind);return event;
}
static Event boundary(int dx,int dy,int duration){
 Swipe s;auto c=context();arm(s,c);s.update({Kind::contact,50,80},c,40);s.update({Kind::contact,75,80},c,60);s.update({Kind::contact,50+dx,80+dy},c,80);
 for(int t=120;t<40+duration;t+=40)s.update({Kind::contact,50+dx,80+dy},c,t);
 s.update({Kind::released},c,40+duration);return s.update({Kind::released},c,60+duration);
}
// Frozen pre-instrumentation semantics: reason reporting must not accept new packets.
static Sample legacy_decode(const uint8_t* b,unsigned length){
 if(!b||length!=6||b[0]>1)return {};
 if(!b[0])return {Kind::released,0,0};
 const int x=((b[1]&15)<<8)|b[2],y=((b[3]&15)<<8)|b[4];
 if(x>233||y>233)return {};
 return {Kind::contact,x,y};
}
static void rejection_tests(){
 const unsigned coords[]={0,1,232,233,234,255,256,465,466,4095};
 for(unsigned count=0;count<256;count++)for(unsigned x:coords)for(unsigned y:coords)for(unsigned flags:{0u,0x40u,0x80u,0xf0u}){
  uint8_t b[]={static_cast<uint8_t>(count),static_cast<uint8_t>((x>>8)|flags),static_cast<uint8_t>(x),static_cast<uint8_t>((y>>8)|flags),static_cast<uint8_t>(y),0xff};
  const auto previous=legacy_decode(b,6),current=decode(b,6);const auto annotated=decode_packet(b,6);
  assert(previous.kind==current.kind&&previous.x==current.x&&previous.y==current.y);
  assert(annotated.sample.kind==current.kind&&annotated.sample.x==current.x&&annotated.sample.y==current.y);
  const unsigned expected=count>1?static_cast<unsigned>(reject_count):count==0?0u:(x>233?static_cast<unsigned>(reject_x):0u)|(y>233?static_cast<unsigned>(reject_y):0u);
  assert(annotated.rejection==expected);
 }
 uint8_t b[6]{};assert(decode_packet(nullptr,6).rejection==reject_packet);
 for(unsigned n:{0u,1u,5u,7u,100u})assert(decode_packet(b,n).rejection==reject_packet);
 for(unsigned reason:{2u,4u,8u,12u}){
  Sampler sampler;Sample sample;unsigned reads=0;
  auto read=[&](uint8_t reg,uint8_t* out,unsigned){if(reg!=2)return true;++reads;out[0]=reason==2?2:1;out[2]=(reason&4)?234:0;out[4]=(reason&8)?234:0;return true;};auto write=[](uint8_t,uint8_t){return true;};
  for(int t=0;t<=60;t+=20)assert(sampler.poll(t,sample,read,write));
  assert(sampler.poll(80,sample,read,write)&&sample.kind==Kind::error);assert(!sampler.poll(329,sample,read,write)&&reads==1);
  for(int n=0;n<65540;n++)assert(sampler.poll(330+250LL*n,sample,read,write)&&sample.kind==Kind::error);
  const auto& stats=sampler.stats();assert(stats.invalid==UINT16_MAX&&stats.invalidCount==((reason&2)?UINT16_MAX:0)&&stats.invalidX==((reason&4)?UINT16_MAX:0)&&stats.invalidY==((reason&8)?UINT16_MAX:0));
  assert(stats.ioErrors==0&&stats.contacts==0&&stats.releases==0&&stats.setupAttempts==4);
  assert(sampler.poll(100000000,sample,read,write));assert(stats.gapMs==UINT16_MAX&&stats.maxGapMs==UINT16_MAX);
  Swipe swipe;char top[48],bottom[48];diagnostic_lines(top,bottom,sampler,swipe,false,0x1ff);assert(std::strlen(top)<=37&&std::strlen(bottom)<=30);assert(std::strstr(bottom,"Zff"));
  if(reason==2)assert(std::strstr(top,"N999 H0 V0"));
  if(reason==4)assert(std::strstr(top,"N0 H999 V0"));
  if(reason==8)assert(std::strstr(top,"N0 H0 V999"));
  if(reason==12)assert(std::strstr(top,"N0 H999 V999"));
 }
}
int main(){
 rejection_tests();
 // Reversal now follows the finger. Release authority uses only final net
 // distance and still requires all original temporal/vertical/release guards.
 for(unsigned rotation=0;rotation<4;rotation++)for(int end:{80,130,180}){
  Swipe s;auto c=context(rotation);arm(s,c);
  for(auto [t,x]:std::vector<std::pair<int,int>>{{40,130},{80,70},{120,100},{160,end}}){
   assert(!s.update(finger(x,100,rotation),c,t).kind);
   assert(s.tracking()&&s.displacement()==x-130);
  }
  assert(!s.update({Kind::released},c,180).kind);
  const auto event=s.update({Kind::released},c,200);
  assert(event.kind==(end==80?2u:end==180?1u:0u));
  assert(!s.update({Kind::released},c,220).kind);
 }
 {Swipe s;auto c=context();arm(s,c);s.update({Kind::contact,180,100},c,40);s.update({Kind::contact,80,100},c,80);
  s.expire(181);assert(!s.tracking()&&!s.armed()&&s.displacement()==0);
  assert(!s.update({Kind::released},c,200).kind);assert(!s.update({Kind::released},c,220).kind);
 }
 {Swipe s;auto c=context();arm(s,c);s.update({Kind::contact,180,100},c,40);s.update({Kind::contact,80,100},c,80);
  s.update({Kind::released},c,120);s.update({Kind::contact,70,100},c,130);
  assert(!s.tracking());assert(!s.update({Kind::released},c,160).kind);assert(!s.update({Kind::released},c,180).kind);
 }
 assert(!boundary(49,0,80).kind);assert(boundary(50,0,80).kind==1);assert(!boundary(50,0,79).kind);
 assert(boundary(50,25,800).kind==1);assert(!boundary(50,0,801).kind);assert(!boundary(50,26,80).kind);
 uint8_t bytes[]={1,0xe0,100,0xa0,120,0};auto decoded=decode(bytes,6);assert(decoded.kind==Kind::contact&&decoded.x==100&&decoded.y==120);
 assert(decode(nullptr,6).kind==Kind::error);assert(decode(bytes,5).kind==Kind::error);
 bytes[0]=2;assert(decode(bytes,6).kind==Kind::error);bytes[0]=1;bytes[2]=234;assert(decode(bytes,6).kind==Kind::error);bytes[0]=0;assert(decode(bytes,6).kind==Kind::released);
 assert(orient({Kind::contact,-1,0},0).kind==Kind::error);assert(orient({Kind::released},4).kind==Kind::error);
 for(unsigned rotation=0;rotation<4;rotation++)for(bool left:{false,true}){const auto e=run(rotation,left);assert(e.kind==(left?2u:1u)&&e.boot==123&&e.shown==40);assert(!run(rotation,left,10).kind);}
 for(unsigned fault=0;fault<13;fault++){
  Swipe s;auto c=context();arm(s,c);s.update({Kind::contact,180,100},c,40);s.update({Kind::contact,150,100},c,80);
  if(fault==0)c.rotation=1;else if(fault==1)c.selected=1;else if(fault==2)c.count=4;else if(fault==3)c.boot=124;else if(fault==4)c.source=2;else if(fault==5)c.generation=1;
  else if(fault==6)c.enabled=false;else if(fault==7)c.shown=0;else if(fault==8)s.update({},c,100);else if(fault==9)c.roster++;
  else if(fault==10)s.update({Kind::contact,120,100},c,400);else if(fault==11)s.update({Kind::contact,120,100},c,60);else c.selection++;
  s.guard(c);assert(!s.update({Kind::contact,80,100},c,120).kind);assert(!s.update({Kind::released},c,160).kind);assert(!s.update({Kind::released},c,180).kind);
 }
 {Swipe s;auto c=context();s.update({Kind::contact,180,100},c,0);s.update({Kind::contact,70,100},c,80);s.update({Kind::released},c,100);assert(!s.update({Kind::released},c,120).kind);} // held at boot
 {Swipe s;auto c=context();arm(s,c);s.update({Kind::contact,100,100},c,40);s.update({Kind::contact,102,102},c,80);s.update({Kind::contact,98,100},c,120);s.update({Kind::released},c,160);assert(!s.update({Kind::released},c,180).kind);} // tap/jitter
 {Swipe s;auto c=context();arm(s,c);s.update({Kind::contact,180,100},c,40);for(int t=80;t<920;t+=40)s.update({Kind::contact,70,100},c,t);s.update({Kind::released},c,920);assert(!s.update({Kind::released},c,940).kind);} // long hold
 {Swipe s;auto c=context();arm(s,c);s.update({Kind::contact,180,100},c,40);c.shown=41;s.update({Kind::contact,150,100},c,80);c.shown=42;s.update({Kind::contact,90,100},c,120);s.update({Kind::released},c,160);auto e=s.update({Kind::released},c,180);assert(e.kind==2&&e.shown==40);} // retain origin frame, not heartbeat
 {Sampler sampler;Sample sample;std::vector<std::pair<unsigned,unsigned>> writes;unsigned reads=0;bool failed=false;
  auto read=[&](uint8_t reg,uint8_t* out,unsigned n){++reads;if(reg==0xa7){assert(n==3);return true;}assert(reg==2&&n==6);if(failed)return false;out[0]=0;return true;};
  auto write=[&](uint8_t reg,uint8_t value){writes.emplace_back(reg,value);return true;};
  for(int t=0;t<80;t+=20){assert(sampler.poll(t,sample,read,write));assert(sample.kind==Kind::error);assert(!sampler.poll(t+1,sample,read,write));}
  assert(sampler.ready());assert((writes==std::vector<std::pair<unsigned,unsigned>>{{0,0},{0xfa,0x20},{0xed,20}}));
  assert(sampler.poll(80,sample,read,write)&&sample.kind==Kind::released);failed=true;assert(sampler.poll(100,sample,read,write)&&sample.kind==Kind::error);
  unsigned before=reads;assert(!sampler.poll(349,sample,read,write)&&reads==before);assert(sampler.poll(350,sample,read,write));assert(writes.size()==3);
 }
 {Swipe s;auto c=context();arm(s,c);s.update({Kind::contact,180,100},c,40);s.update({Kind::contact,150,100},c,80);s.update({Kind::contact,80,100},c,120);s.update({Kind::released},c,160);s.update({},c,170);assert(!s.update({Kind::released},c,180).kind);assert(!s.update({Kind::released},c,200).kind);}
 for(unsigned fail=0;fail<4;fail++){
  Sampler sampler;Sample sample;unsigned attempts=0;std::vector<unsigned> operations;
  auto read=[&](uint8_t reg,uint8_t*,unsigned){operations.push_back(0x100+reg);return attempts++!=fail;};
  auto write=[&](uint8_t reg,uint8_t){operations.push_back(reg);return attempts++!=fail;};
  int t=0;for(unsigned stage=0;stage<=fail;stage++,t+=20)assert(sampler.poll(t,sample,read,write));
  t-=20;assert(!sampler.ready()&&!sampler.halted()&&sampler.stage()==fail&&attempts==fail+1);
  assert(!sampler.poll(t+249,sample,read,write)&&attempts==fail+1);t+=250;
  assert(sampler.poll(t,sample,read,write));assert(operations[fail]==operations[fail+1]);
  assert(!sampler.poll(t,sample,read,write));
  while(!sampler.ready()){t+=20;const auto before=attempts;assert(sampler.poll(t,sample,read,write)&&attempts==before+1);}
  assert(attempts==5&&sampler.stats().setupAttempts==5&&sampler.stats().setupFailures==1&&sampler.stats().stageFailures[fail]==1);
 }
 {Sampler sampler;Sample sample;unsigned attempts=0;auto read=[](uint8_t,uint8_t*,unsigned){return false;};auto write=[&](uint8_t,uint8_t){++attempts;return false;};
  for(int t=0;t<=2000;t++)if(sampler.poll(t,sample,read,write))assert(sample.kind==Kind::error);
  assert(attempts==9&&!sampler.ready()&&!sampler.halted()&&sampler.stage()==0);
  assert(sampler.stats().contacts==0&&sampler.stats().releases==0);
  assert(sampler.poll(100000,sample,read,write)&&attempts==10); // No catch-up loop.
  int64_t now=100000;for(unsigned n=0;n<65540;n++){now+=250;sampler.poll(now,sample,read,write);}
  assert(sampler.stats().setupAttempts==UINT16_MAX&&sampler.stats().setupFailures==UINT16_MAX&&sampler.stats().stageFailures[0]==UINT16_MAX);
  Swipe swipe;char top[48],bottom[48];diagnostic_lines(top,bottom,sampler,swipe,true);assert(std::strstr(top,"T0 F999 I0 B0 D0")&&std::strstr(bottom,"C0 U0 A0 X0 G1 S0"));assert(std::strlen(top)<=37&&std::strlen(bottom)<=30);
 }
 for(int which=0;which<3;which++){
  Sampler sampler;Sample sample;unsigned ops=0;auto read=[&](uint8_t,uint8_t*,unsigned){++ops;return true;};auto write=[&](uint8_t,uint8_t){++ops;return true;};
  if(which==0)assert(sampler.poll(-1,sample,read,write));
  if(which==1)assert(sampler.poll(INT64_MAX-249,sample,read,write));
  if(which==2){assert(sampler.poll(100,sample,read,write));assert(sampler.poll(99,sample,read,write));}
  assert(sampler.halted()&&ops==(which==2?1u:0u));assert(!sampler.poll(1000,sample,read,write));
 }
 {Sampler sampler;Swipe swipe;auto c=context();Sample wanted{Kind::released};unsigned failures=1,ops=0;
  auto read=[&](uint8_t reg,uint8_t* out,unsigned){++ops;if(reg==0xa7)return true;if(wanted.kind==Kind::error)return false;out[0]=wanted.kind==Kind::contact;out[2]=wanted.x;out[4]=wanted.y;return true;};
  auto write=[&](uint8_t,uint8_t){++ops;if(failures){--failures;return false;}return true;};
  auto step=[&](int64_t t,Sample next){wanted=next;Sample sampled;if(!swipe.guard(c)||!sampler.poll(t,sampled,read,write))return Event{};return swipe.update(sampled,c,t);};
  assert(!step(0,{}).kind);c.enabled=false;const auto before=ops;for(int t=20;t<500;t+=20)assert(!step(t,{}).kind);assert(ops==before);c.enabled=true;
  for(int t=500;t<=560;t+=20)assert(!step(t,{}).kind);assert(sampler.ready());
  assert(!step(580,{}).kind); // Sample failure; no release inferred.
  assert(!step(830,{Kind::released}).kind);assert(!swipe.armed()); // First post-backoff success discarded by gap guard.
  assert(!step(850,{Kind::released}).kind);assert(!swipe.armed());assert(!step(870,{Kind::released}).kind);assert(swipe.armed());
  assert(!step(890,{Kind::contact,180,100}).kind);assert(!step(930,{Kind::contact,150,100}).kind);assert(!step(970,{Kind::contact,90,100}).kind);
  assert(!step(1010,{Kind::released}).kind);auto event=step(1030,{Kind::released});assert(event.kind==2&&event.shown==40&&swipe.emitted()==1);
  assert(sampler.stats().ioErrors==1&&sampler.stats().contacts==3&&sampler.stats().releases==5&&sampler.stats().maxGapMs==250);
  char top[48],bottom[48];diagnostic_lines(top,bottom,sampler,swipe,true);assert(std::strstr(top,"T4 F1 I1 B0 D20"));assert(std::strstr(bottom,"C3 U5 A1 X0 G1 S1"));
 }
 {Sampler sampler;Swipe swipe;Sample sample;auto c=context();unsigned reads=0;
  auto read=[&](uint8_t reg,uint8_t* out,unsigned){if(reg==0xa7)return true;++reads;if(reads%5==0)return false;out[0]=1;out[2]=120;out[4]=100;return true;};auto write=[](uint8_t,uint8_t){return true;};
  for(int t=0;t<20000;t+=20)if(sampler.poll(t,sample,read,write))assert(!swipe.update(sample,c,t).kind);
  assert(sampler.ready()&&sampler.stats().contacts>0&&sampler.stats().ioErrors>0&&sampler.stats().releases==0&&!swipe.armed()&&swipe.emitted()==0);
 }
 {Sampler sampler;Sample sample;auto read=[](uint8_t reg,uint8_t* out,unsigned){if(reg==2)out[0]=2;return true;};auto write=[](uint8_t,uint8_t){return true;};
  for(int t=0;t<=80;t+=20)sampler.poll(t,sample,read,write);assert(sample.kind==Kind::error&&sampler.stats().invalid==1&&sampler.stats().ioErrors==0&&sampler.stats().releases==0);
  assert(!sampler.poll(329,sample,read,write));assert(sampler.poll(330,sample,read,write));
 }
 {uint16_t n=UINT16_MAX-1;saturate(n);saturate(n);assert(n==UINT16_MAX);}
}
