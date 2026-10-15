#include "../firmware/pet/main/touch-timing.h"
#include "../firmware/pet/main/touch.h"
#include "../firmware/pet/main/touch-usb.h"
#include <cassert>
#include <cstring>
#include <climits>
#include <initializer_list>
using namespace watcher_touch;
static Context context(){return {true,1,10,0,3,0,1,0,1,1};}
static void arm(Swipe& s,int64_t at=0){s.update({Kind::released},context(),at);s.update({Kind::released},context(),at+20);assert(s.armed());}
static void drag(Swipe& s){arm(s);s.update({Kind::contact,150,100},context(),40);s.update({Kind::contact,120,100},context(),80);s.update({Kind::contact,70,100},context(),120);assert(s.tracking());}
static void snapshot(const Timing& t,uint16_t (&out)[48]){Sampler sampler;Swipe swipe;t.snapshot(out,sampler,swipe,true);}
int main(){
 static_assert(sizeof(Timing)<=640,"Fixed extended timing RAM budget");
 Timing t;uint16_t v[48]{};
 {Timing delayed;Sampler sampler;Swipe swipe;uint16_t all[192]{};
  delayed.issue(2,0);delayed.ack(3,1,20);delayed.paint(1,40,true,1);
  delayed.snapshot_full(all,sampler,swipe,true,40);assert(all[87]==0&&(all[80]&2));
  delayed.paint(1,60,true,2);delayed.snapshot_full(all,sampler,swipe,true,60);assert(all[87]==1&&all[32]==40);
  delayed.issue(4,100);delayed.ack(4,1,110);delayed.issue(5,120);delayed.ack(5,1,130);
  delayed.paint(1,140,true,4);delayed.snapshot_full(all,sampler,swipe,true,140);assert(all[87]==1&&(all[80]&2));
  delayed.paint(1,150,true,5);delayed.snapshot_full(all,sampler,swipe,true,150);assert(all[87]==2&&all[32]==20);
 }

 t.loop(0);t.loop(100);t.loop(201);t.loop(90000);snapshot(t,v);assert(v[0]==(90000&65535)&&v[1]==1&&v[2]==65535&&v[3]==65535&&v[4]==2);
 t.render(0,80);t.render(100,201);snapshot(t,v);assert(v[5]==101&&v[6]==101&&v[7]==1&&v[47]==2);
 t.read(10,12);t.read(110,115);t.read(211,231);snapshot(t,v);assert(v[8]==20&&v[9]==101&&v[10]==1);
 t.gate(0,false,2);t.gate(100,false,8);t.gate(350,true,0);snapshot(t,v);assert(v[18]==0&&v[19]==10&&v[20]==350&&v[46]==350);
 t.issue(3,400);t.ack(2,1,500);snapshot(t,v);assert(v[30]==0);t.ack(3,1,800);t.paint(0,810);snapshot(t,v);assert(v[32]==0);t.paint(1,950);snapshot(t,v);assert(v[29]==1&&v[30]==400&&v[31]==400&&v[32]==150&&v[33]==150);
 t.issue(4,1000);t.retire();t.ack(4,2,2000);snapshot(t,v);assert(v[29]==2&&v[30]==400);
 t.metadata(true,0);t.metadata(true,80);t.metadata(false,120);snapshot(t,v);assert(v[34]==1&&v[35]==120);
 t.phase(38,0,30);t.phase(39,0,40);t.phase(40,0,50);t.phase(41,0,60);t.phase(42,0,70);t.phase(48,0,999);t.phase(UINT_MAX,0,999);t.frame_age(90000);t.denied();snapshot(t,v);assert(v[36]==1&&v[37]==65535);for(unsigned i=38;i<=42;i++)assert(v[i]==30+(i-38)*10);
 for(unsigned i=0;i<70000;i++){t.denied();t.loop(100000+i);t.render(0,81);}
 snapshot(t,v);assert(v[36]==65535&&v[46]==350&&v[47]==65535&&v[7]==65535);
 // Bad diagnostic clocks never mutate gesture authority, overflow, or index OOB.
 t.loop(-1);t.loop(0);t.read(-1,0);t.read(INT64_MAX-2,INT64_MAX);t.gate(-1,false,2);t.metadata(false,-1);t.phase(38,INT64_MAX,0);
 {Swipe s;drag(s);s.expire(221);assert(!s.tracking()&&s.cancellation_stats().counts[0]==1&&s.cancellation_stats().at==221);s.expire(300);assert(s.cancellation_stats().counts[0]==1);}
 {Swipe s;drag(s);auto c=context();c.selection=2;assert(!s.guard(c,125));assert(s.cancellation_stats().counts[1]==1&&s.cancellation_stats().at==125);}
 {Swipe s;drag(s);s.update({Kind::error},context(),140);assert(s.cancellation_stats().counts[2]==1);}
 {Swipe s;arm(s);for(int i=0;i<=9;i++)s.update({Kind::contact,100,100},context(),40+i*100);assert(s.cancellation_stats().counts[3]==1);}
 {Swipe s;drag(s);s.update({Kind::contact,70,126},context(),140);assert(s.cancellation_stats().counts[4]==1);}
 {Swipe s;arm(s);s.update({Kind::contact,100,100},context(),40);s.update({Kind::contact,99,101},context(),60);s.update({Kind::released},context(),80);s.update({Kind::released},context(),100);assert(s.cancellation_stats().counts[5]==1&&s.cancellation_stats().release_flags==15);}
 {Swipe s;for(int i=0;i<70000;i++)s.update({Kind::contact,100,100},context(),i+1);assert(s.cancellation_stats().counts[6]==65535&&s.cancellation_stats().reason==0);}
 {Swipe s;drag(s);s.update({Kind::released},context(),140);s.update({Kind::contact,70,100},context(),150);assert(s.cancellation_stats().counts[7]==1);}
 // Query is one-shot/rate-limited, boot-bound, no replay/transport authority.
 UsbDiagnosticGate gate;char query[64];const int head=std::snprintf(query,sizeof query,"TQ3 1 7 ");const int tail=std::snprintf(query+head,sizeof(query)-head,"%u",watcher_wire::crc(query,head));const size_t n=head+tail;
 assert(!gate.request(query,n,0,1,false));assert(!gate.request(query,n,0,2,true));assert(gate.request(query,n,0,1,true));assert(gate.version()==3&&gate.mode()==0);assert(!gate.take(100,1,true,false));assert(gate.take(101,1,true,true)==7);assert(!gate.take(102,1,true,true));assert(!gate.request(query,n,1000,1,true));
 // Worst-case line fits unchanged192-byte framing; emitted for JS cross-check.
 for(auto& value:v)value=65535;v[17]=31;v[18]=v[19]=255;v[43]=0x0f09;
 char line[192];auto length=timing_line(line,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,v);assert(length&&length<192&&line[length-1]=='\n');std::fwrite(line,1,length,stdout);
 Timing empty;Sampler sampler;Swipe swipe;empty.snapshot(v,sampler,swipe,false);length=timing_line(line,1,7,10,9,v);assert(length&&length<192);std::fwrite(line,1,length,stdout);
}
