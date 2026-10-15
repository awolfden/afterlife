#include "../firmware/pet/main/touch-timing.h"
#include "../firmware/pet/main/touch.h"
#include "../firmware/pet/main/touch-usb.h"
#include <cassert>
#include <cstring>
#include <climits>
using namespace watcher_touch;
static uint32_t word(const uint16_t* v,unsigned i){return v[i]|(static_cast<uint32_t>(v[i+1])<<16);}
static void snap(const Timing& t,uint16_t (&v)[192],int64_t now=1000){Sampler sampler;Swipe swipe;t.snapshot_full(v,sampler,swipe,true,now);}
int main(){
 uint16_t v[192]{};
 {Timing t;t.gate(0,false,2);t.gate(100,false,8);t.gate(350,true,0);snap(t,v);
  assert(word(v,49)==350&&v[48]==1&&v[52]==10&&v[19]==10&&v[46]==350);
  assert(v[56]==1&&word(v,57)==100&&v[62]==1&&word(v,63)==250);
  t.gate(400,false,2);t.gate(440,true,0);t.gate(500,false,10);t.gate(550,true,0);snap(t,v);
  assert(v[48]==3&&word(v,49)==440&&v[56]==3&&word(v,57)==190&&v[62]==2&&word(v,63)==300);
  t.gate(600,false,128,true);t.gate(625,true,0);snap(t,v);assert(v[77]==1&&word(v,78)==25&&v[74]==0&&v[52]==256);
  t.gate(700,false,2);t.gate(600,true,0);snap(t,v);assert(v[95]==1&&word(v,49)==465);
 }
 {Timing t;t.gate(0,false,1);t.gate(INT64_MAX,true,0);snap(t,v);assert(word(v,49)==UINT32_MAX&&word(v,54)==UINT32_MAX);
  for(int i=0;i<70000;i++){t.gate(i*2,false,1);t.gate(i*2+1,true,0);}snap(t,v);assert(v[48]==65535&&v[53]==65535&&word(v,49)==UINT32_MAX);
 }
 {Timing t;t.phase(41,0,200);t.phase(41,1000,1150);t.phase(39,1200,1330);t.phase(39,1400,1450);t.read(1500,1530);t.read(1800,1802);snap(t,v);
  assert(v[41]==200&&v[108]==2&&v[109]==150&&word(v,110)==1150);
  assert(v[100]==2&&v[101]==50&&word(v,102)==1450);
  assert(v[116]==1&&v[117]==30&&word(v,118)==1530&&v[8]==30&&v[9]==300);
  t.phase(41,2000,2020);snap(t,v);assert(v[108]==2); // threshold strictly >20
 }
 {Timing t;t.loop(0);t.phase(41,0,60);t.phase(42,60,90);t.phase(38,90,91);t.phase(39,91,92);t.phase(40,92,93);t.read(93,95);t.render(95,180);t.paint(0,180);t.loop(190);snap(t,v);
  assert(v[144]==1&&v[145]==190&&word(v,146)==190&&v[148]==1&&v[149]==1&&v[150]==1&&v[151]==60&&v[152]==30&&v[153]==2&&v[154]==85&&v[155]==10&&v[157]==1&&v[158]==1);
  t.loop(200);snap(t,v);assert(v[144]==1); // retained last slow loop, not overwritten by a fast loop
 }
 {Timing t;t.issue(1,400);snap(t,v,450);assert(v[80]==1&&word(v,81)==50&&v[85]==0);
  t.ack(1,2,500);snap(t,v,580);assert(v[80]==2&&word(v,83)==80&&v[85]==1);
  t.paint(1,600);snap(t,v,610);assert(v[80]==6&&v[87]==0&&word(v,89)==1&&word(v,93)==10);
  t.retire();snap(t,v);assert(v[80]==4&&v[88]==1&&v[87]==0);
  t.issue(2,1000);t.retire();snap(t,v);assert(v[86]==1);
  t.issue(3,1100);t.ack(3,2,1200);t.paint(2,1300);snap(t,v,1350);assert(v[85]==2&&v[87]==1&&v[88]==1&&word(v,89)==2&&word(v,93)==50);
 }
 {Timing t;t.render(0,1);t.render(10,11);snap(t,v);assert(v[47]==2&&word(v,89)==0&&!(v[80]&4));t.paint(0,15,false);snap(t,v,20);assert(v[47]==2&&word(v,89)==1&&word(v,93)==5);}
 {Swipe s;Sampler sampler;Timing t;Context c{true,1,10,0,3,0,1,0,1,1};
  s.update({Kind::released},c,0);s.update({Kind::released},c,20);
  s.update({Kind::contact,100,100},c,40);s.update({Kind::contact,99,101},c,60);s.update({Kind::released},c,80);s.update({Kind::released},c,100);
  s.update({Kind::contact,100,100},c,120);s.update({Kind::contact,90,100},c,160);s.update({Kind::contact,70,100},c,200);s.update({Kind::released},c,220);s.update({Kind::released},c,240);
  s.update({Kind::contact,100,100},c,260);s.update({Kind::contact,100,126},c,280);
  t.snapshot_full(v,sampler,s,true,300);assert(v[43]==0x0f05&&v[128]==4&&word(v,129)==240&&v[131]==1&&v[132]==1&&v[133]==2&&v[134]==1);
 }
 {UsbDiagnosticGate gate;char q[64];const int h=std::snprintf(q,sizeof q,"TQ4 1 9 ");const int tail=std::snprintf(q+h,sizeof(q)-h,"%u",watcher_wire::crc(q,h));assert(gate.request(q,h+tail,0,1,true));assert(gate.version()==4&&gate.take(0,1,true,true)==9&&!gate.take(1,1,true,true));}
 // Worst-case four-line snapshot: every framing line stays below192 bytes.
 for(auto& n:v)n=65535;v[17]=31;v[18]=v[19]=255;v[43]=0x0f09;v[51]=v[52]=v[156]=511;v[80]=7;v[128]=15;v[157]=v[158]=1;v[160]=9;
 char burst[768];const auto n=timing_full_lines(burst,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,v);assert(n&&n<=760);
 size_t start=0;unsigned lines=0;for(size_t i=0;i<n;i++)if(burst[i]=='\n'){assert(i-start<192);start=i+1;++lines;}assert(lines==4&&start==n);std::fwrite(burst,1,n,stdout);
}
