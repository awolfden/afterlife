#define WATCHER_TOUCH_DIAGNOSTICS 1
#include "../firmware/pet/main/touch-usb.h"
#include <cassert>
#include <string>
#include <iostream>
using namespace watcher_touch;
static std::string query(uint32_t boot,uint32_t id){std::string s="TQ1 "+std::to_string(boot)+" "+std::to_string(id)+" ";return s+std::to_string(watcher_wire::crc(s.data(),s.size()));}
static bool request(UsbDiagnosticGate& g,int64_t at,uint32_t id,bool allowed=true){const auto s=query(123,id);return g.request(s.data(),s.size(),at,123,allowed);}
int main(){
 {UsbDiagnosticGate g;assert(request(g,0,7));assert(!request(g,0,8));assert(g.take(0,123,true,false)==0);assert(g.take(999,123,true,true)==7);assert(g.take(999,123,true,true)==0);assert(!request(g,1000,7));assert(request(g,1000,8));assert(g.take(1000,123,false,true)==0);assert(g.take(1001,123,true,true)==0);}
 {UsbDiagnosticGate g;assert(request(g,0,7));assert(g.take(1001,123,true,true)==0);assert(request(g,1001,8));assert(g.take(1001,124,true,true)==0);}
 {UsbDiagnosticGate g;assert(request(g,0,7));assert(g.take(0,123,true,true)==7);assert(!request(g,999,8));assert(request(g,1000,8));}
 {UsbDiagnosticGate g;assert(!request(g,0,7,false));assert(g.take(0,123,true,true)==0);assert(request(g,1,7));assert(g.take(1,123,true,true)==7);}
 for(int which=0;which<3;which++){UsbDiagnosticGate g;if(which==0)assert(!request(g,-1,7));if(which==1)assert(!request(g,INT64_MAX-999,7));if(which==2){assert(request(g,10,7));assert(g.take(9,123,true,true)==0);}assert(!request(g,1000,8));}
 for(const auto& s:std::initializer_list<std::string>{"", "Q1", "TQ1",query(124,7),query(123,0),query(123,7)+" ",query(123,7)+"\n","TQ1 123 4294967296 0","TQ1 -123 7 0",std::string(65,'x')}){UsbDiagnosticGate g;assert(!g.request(s.data(),s.size(),0,123,true));assert(g.take(0,123,true,true)==0);}
 {auto s=query(123,7);s.back()=s.back()=='0'?'1':'0';UsbDiagnosticGate g;assert(!g.request(s.data(),s.size(),0,123,true));}
 Sampler sampler;Swipe swipe;char line[192];auto n=usb_diagnostic_line(line,123,7,42,40,sampler,swipe,false,64);assert(n>0&&n<192&&line[n-1]=='\n');std::cout.write(line,n);
 auto& stats=const_cast<SamplerStats&>(sampler.stats());stats.setupFailures=stats.ioErrors=stats.invalid=stats.gapMs=stats.invalidCount=stats.invalidX=stats.invalidY=stats.contacts=stats.releases=stats.maxGapMs=UINT16_MAX;
 n=usb_diagnostic_line(line,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,sampler,swipe,true,255);assert(n>0&&n<192);std::cout.write(line,n);
}
