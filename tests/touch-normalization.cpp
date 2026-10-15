// Counter assertions intentionally exercise the opt-in diagnostic build.
#define WATCHER_TOUCH_DIAGNOSTICS 1
#include "../firmware/pet/main/touch-normalization.h"
#include "../firmware/pet/main/touch.h"
#include "../firmware/pet/main/touch-range.h"
#include <array>
#include <cassert>
#include <cstring>
using namespace watcher_touch;
using Packet=std::array<uint8_t,6>;
static Packet packet(unsigned x,unsigned y,unsigned count=1,unsigned flags=0){return {uint8_t(count),uint8_t(flags|(x>>8)),uint8_t(x),uint8_t(flags|(y>>8)),uint8_t(y),255};}
static Context context(unsigned rotation=0){return {true,123,42,0,3,rotation,1,0};}
static Event feed(Swipe& s,const Packet& p,int64_t at,unsigned rotation=0){return s.update(decode_measured_packet(p.data(),p.size()).sample,context(rotation),at);}
static void arm(Swipe& s,unsigned rotation=0){auto b=packet(4095,4095,0);assert(!feed(s,b,0,rotation).kind);assert(!feed(s,b,20,rotation).kind);assert(s.armed());}
static Event release(Swipe& s,unsigned rotation=0){const auto b=packet(4095,4095,0);assert(!feed(s,b,200,rotation).kind);return feed(s,b,220,rotation);}
static Packet inverse(unsigned x,unsigned y,unsigned rotation){if(rotation==1)return packet(2*(233-y),2*x);if(rotation==2)return packet(2*(233-x),2*(233-y));if(rotation==3)return packet(2*y,2*(233-x));return packet(2*x,2*y);}
int main(){
 for(unsigned flags:{0u,0x40u,0x80u,0xf0u})for(unsigned x=0;x<512;x++)for(unsigned y=0;y<512;y++){
  auto b=packet(x,y,1,flags);const auto old=decode_packet(b.data(),b.size()),d=decode_measured_packet(b.data(),b.size());
  const unsigned legacy=(x>233?reject_x:0u)|(y>233?reject_y:0u),measured=(x>460?reject_x:0u)|(y>461?reject_y:0u);
  assert(old.rejection==legacy&&d.rejection==measured);assert(old.sample.kind==(legacy?Kind::error:Kind::contact));assert(d.sample.kind==(measured?Kind::error:Kind::contact));
  if(!legacy)assert(old.sample.x==int(x)&&old.sample.y==int(y));
  if(!measured){auto half=packet(x/2,y/2);const auto expected=decode(half.data(),half.size());assert(d.sample.x==expected.x&&d.sample.y==expected.y);for(unsigned r=0;r<4;r++){const auto a=orient(d.sample,r),e=orient(expected,r);assert(a.kind==Kind::contact&&a.x==e.x&&a.y==e.y&&a.x>=0&&a.x<=233&&a.y>=0&&a.y<=233);}}
 }
 for(unsigned count:{0u,2u,255u})for(unsigned x:{0u,233u,460u,461u,462u,512u,1023u,2047u,4095u}){
  auto b=packet(x,4095,count,0xf0);const auto a=decode_packet(b.data(),b.size()),d=decode_measured_packet(b.data(),b.size());assert(a.rejection==d.rejection&&a.sample.kind==d.sample.kind);assert(d.sample.kind==(count?Kind::error:Kind::released));
 }
 for(unsigned x:{512u,1023u,2047u,4095u}){auto b=packet(x,x);auto d=decode_measured_packet(b.data(),b.size());assert(d.sample.kind==Kind::error&&d.rejection==(reject_x|reject_y));}
 auto b=packet(100,100);assert(decode_measured_packet(nullptr,6).rejection==reject_packet);for(unsigned n:{0u,1u,5u,7u})assert(decode_measured_packet(b.data(),n).rejection==reject_packet);
 for(unsigned r=0;r<4;r++)for(bool left:{false,true}){Swipe s;arm(s,r);for(unsigned i=0;i<4;i++){unsigned x=left?160-i*30:60+i*30;assert(!feed(s,inverse(x,100,r),40+i*40,r).kind);}const auto e=release(s,r);assert(e.kind==(left?2u:1u)&&e.boot==123&&e.shown==42);assert(!feed(s,packet(0,0,0),240,r).kind&&s.emitted()==1);}
 // Floor parity at displacement, vertical-drift and reversal boundaries.
 for(unsigned parity:{0u,1u})for(int delta:{98,99,100,101,-98,-99,-100,-101}){Swipe s;arm(s);const int start=delta>0?150+parity:300+parity,end=start+delta;for(int i=0;i<4;i++)feed(s,packet(start+delta*i/3,220),40+i*40);const auto e=release(s);assert(bool(e.kind)==(std::abs(end/2-start/2)>=50));}
 for(unsigned parity:{0u,1u})for(unsigned dy:{49u,50u,51u,52u}){Swipe s;arm(s);for(unsigned i=0;i<4;i++)feed(s,packet(100+100*i,200+parity+dy*i/3),40+i*40);const auto e=release(s);assert(bool(e.kind)==((200+parity+dy)/2-(200+parity)/2<=25));}
 for(unsigned parity:{0u,1u})for(unsigned back:{23u,24u,25u,26u,149u,150u,151u,249u,250u,251u}){Swipe s;arm(s);const unsigned peak=350+parity;feed(s,packet(100,220),40);feed(s,packet(peak-50,220),80);feed(s,packet(peak,220),120);feed(s,packet(peak-back,220),160);assert(s.tracking()&&s.displacement()==static_cast<int>((peak-back)/2)-50);assert(bool(release(s).kind)==(std::abs(static_cast<int>((peak-back)/2)-50)>=50));}
 // An outlier cannot become release or preserve a partial gesture.
 {Swipe s;arm(s);feed(s,packet(100,200),40);feed(s,packet(250,200),80);feed(s,packet(461,200),120);feed(s,packet(400,200),160);assert(!release(s).kind&&s.emitted()==0);}
 {Swipe s;arm(s);feed(s,packet(100,200),40);feed(s,packet(250,200),141);feed(s,packet(400,200),160);assert(!release(s).kind);} // >100ms still cancels.
 {Swipe s;arm(s);feed(s,packet(100,200),40);auto c=context();c.enabled=false;s.guard(c);feed(s,packet(400,200),160);assert(!release(s).kind);} // Existing suppression retained.
 // Sampler opt-in is distinct from the always-legacy public decode; range stays raw.
 Sampler sampler;RangeWindow range;assert(range.arm(1,context(),0));Sample sample;Packet current=packet(460,461);int64_t now=0;unsigned reads=0;
 auto read=[&](uint8_t reg,uint8_t* data,unsigned n){if(reg==0xa7){assert(n==3);std::memset(data,0,n);return true;}assert(reg==2&&n==6);++reads;std::memcpy(data,current.data(),6);range.observe(data,n,true,now);return true;};auto write=[](uint8_t,uint8_t){return true;};
 for(now=0;now<80;now+=20)assert(sampler.poll(now,sample,read,write));assert(sampler.ready());assert(sampler.poll(now,sample,read,write));assert(reads==1&&range.snapshot().xMax==460&&range.snapshot().yMax==461);
#ifdef WATCHER_TOUCH_MEASURED_SCALE
 assert(sample.kind==Kind::contact&&sample.x==230&&sample.y==230&&sampler.stats().contacts==1);
 now=100;current=packet(461,462);assert(sampler.poll(now,sample,read,write));assert(sample.kind==Kind::error&&sampler.stats().invalidX==1&&sampler.stats().invalidY==1);
 now=349;assert(!sampler.poll(now,sample,read,write)&&reads==2);now=350;current=packet(4095,4095,0);assert(sampler.poll(now,sample,read,write)&&sample.kind==Kind::released&&reads==3);
#else
 assert(sample.kind==Kind::error&&sampler.stats().invalidX==1&&sampler.stats().invalidY==1&&sampler.stats().contacts==0);now=100;assert(!sampler.poll(now,sample,read,write)&&reads==1);
#endif
}
