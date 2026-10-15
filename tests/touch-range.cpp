#define WATCHER_TOUCH_DIAGNOSTICS 1
#include "../firmware/pet/main/touch-range-usb.h"
#include <cassert>
#include <iostream>
#include <string>
using namespace watcher_touch;
static Context c{true,123,42,0,2,0,1,0};
static void point(RangeWindow& r,unsigned x,unsigned y,int64_t at){const uint8_t b[]={1,uint8_t(0xa0|(x>>8)),uint8_t(x),uint8_t(0xb0|(y>>8)),uint8_t(y),255};r.observe(b,6,true,at);}
static std::string query(unsigned id,unsigned mode){auto s="TQ2 123 "+std::to_string(id)+" "+std::to_string(mode)+" ";return s+std::to_string(watcher_wire::crc(s.data(),s.size()));}
int main(){
 RangeWindow r;point(r,4095,4095,0);assert(r.snapshot().samples==0);
 assert(r.arm(7,c,0));assert(!r.arm(8,c,1));assert(r.snapshot().windows==1);
 const uint8_t release[6]{};r.observe(release,6,true,10);assert(r.snapshot().state==1);
 point(r,350,220,100);point(r,355,217,200);assert(r.snapshot().samples==2);assert(r.snapshot().xMin==350&&r.snapshot().xMax==355&&r.snapshot().yMin==217&&r.snapshot().yMax==220);
 // Above-233 points are summarized, but the unchanged gesture decoder still rejects them.
 const uint8_t bad[]={1,1,94,0,220,0};assert(decode(bad,6).kind==Kind::error);
 r.observe(release,6,true,300);assert(r.snapshot().state==3&&r.snapshot().elapsed==200);
 point(r,4095,4095,400);assert(r.snapshot().samples==2&&r.snapshot().xMax==355);
 auto next=c;next.shown=43;r.guard(next,500);assert(r.snapshot().state==3);next.selected=1;r.guard(next,600);assert(r.snapshot().state==5&&r.snapshot().samples==0);
 for(unsigned key=0;key<7;key++){RangeWindow t;assert(t.arm(1,c,0));auto d=c;switch(key){case 0:d.enabled=false;break;case 1:d.boot++;break;case 2:d.selected++;break;case 3:d.count++;break;case 4:d.rotation++;break;case 5:d.source++;break;case 6:d.generation++;break;}t.guard(d,1);point(t,10,20,2);assert(t.snapshot().state==5&&t.snapshot().samples==0);}
 {RangeWindow t;assert(t.arm(1,c,0));t.guard(c,120000);assert(t.snapshot().state==4);point(t,1,2,120001);assert(t.snapshot().samples==0);}
 {RangeWindow t;assert(t.arm(1,c,0));point(t,1,2,100);point(t,4095,4095,1100);assert(t.snapshot().state==3&&t.snapshot().samples==1&&t.snapshot().xMax==1&&t.snapshot().elapsed==1000);}
 {RangeWindow t;assert(t.arm(1,c,0));for(unsigned i=0;i<70;i++)point(t,i,4095-i,i+1);assert(t.snapshot().samples==50&&t.snapshot().xMax==49&&t.snapshot().state==3);}
 for(unsigned kind=0;kind<4;kind++){RangeWindow t;assert(t.arm(1,c,0));point(t,1,2,1);uint8_t b[]={2,0,1,0,2,0};t.observe(kind==0?nullptr:b,kind==1?5:6,kind!=2,2);assert(t.snapshot().state==5&&t.snapshot().samples==0);}
 for(int64_t at:{int64_t(-1),INT64_MAX}){RangeWindow t;assert(t.arm(1,c,0));t.guard(c,at);assert(!t.arm(2,c,10));assert(t.snapshot().state==5);}
 {RangeWindow t;assert(t.arm(1,c,10));t.guard(c,9);assert(!t.arm(2,c,11));}
 {RangeWindow t;for(unsigned i=0;i<10;i++){assert(t.arm(i+1,c,i*2000));point(t,20,30,i*2000+1);t.observe(release,6,true,i*2000+2);}assert(!t.arm(11,c,20000));assert(t.snapshot().windows==10);}
 {UsbDiagnosticGate g;auto q=query(7,1);assert(g.request(q.data(),q.size(),0,123,true));assert(g.take(0,123,true,true)==7&&g.version()==2&&g.mode()==1);q=query(8,2);assert(!g.request(q.data(),q.size(),1000,123,true));q=query(8,0);assert(g.request(q.data(),q.size(),1000,123,true));assert(!g.take(1001,123,false,true));assert(!g.take(1002,123,true,true));}
 RangeWindow output;assert(output.arm(7,c,0));point(output,350,220,1);point(output,355,217,2);output.observe(release,6,true,1001);
 Sampler sampler;Swipe swipe;char line[192];auto n=usb_range_line(line,123,7,42,42,sampler,swipe,true,0,output.snapshot());assert(n&&n<sizeof(line));std::cout.write(line,n);
 auto& stats=const_cast<SamplerStats&>(sampler.stats());stats.setupFailures=stats.ioErrors=stats.invalid=stats.gapMs=stats.invalidCount=stats.invalidX=stats.invalidY=stats.contacts=stats.releases=stats.maxGapMs=UINT16_MAX;
 RangeSnapshot maximum{UINT32_MAX,3,50,0,4095,0,4095,1000,10};n=usb_range_line(line,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,sampler,swipe,true,255,maximum);assert(n&&n<sizeof(line));std::cout.write(line,n);
}
