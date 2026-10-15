#pragma once
#include "wire.h"
#include "touch.h"
#include <cstdio>
#include <cstring>
namespace watcher_touch {
// Query/reply admission only; no bus operations, persistence, or link authority.
class UsbDiagnosticGate {
 uint32_t pending_=0,boot_=0,last_id_=0;unsigned version_=1,mode_=0;int64_t last_clock_=-1,issued_=-1000;bool halted_=false;
 bool clock(int64_t now){if(halted_||now<0||now>INT64_MAX-1000||(last_clock_>=0&&now<last_clock_)){halted_=true;pending_=0;return false;}last_clock_=now;return true;}
 public:
 unsigned version()const{return version_;}
 unsigned mode()const{return mode_;}
 bool request(const char* line,size_t length,int64_t now,uint32_t boot,bool allowed){
  if(!clock(now))return false;
  if(!allowed){pending_=0;return false;}
  if(!line||length<4||length>64)return false;
  unsigned version=!std::memcmp(line,"TQ1 ",4)?1u:(!std::memcmp(line,"TQ2 ",4)?2u:0u);
#ifdef WATCHER_TOUCH_TIMING_DIAGNOSTICS
  if(!std::memcmp(line,"TQ3 ",4))version=3;
  if(!std::memcmp(line,"TQ4 ",4))version=4;
#endif
  if(!version)return false;
  const unsigned fields=version==2?4u:3u;
  const char* p=line+4;const char* end=line+length;const char* sum=nullptr;uint32_t v[4]{};
  for(unsigned i=0;i<fields;i++){if(i==fields-1)sum=p;if(!watcher_wire::number(p,end,v[i]))return false;if(i!=fields-1&&(p==end||*p++!=' '))return false;}
  if(p!=end||!boot||v[0]!=boot||!v[1]||(version==2&&v[2]>1)||watcher_wire::crc(line,sum-line)!=v[fields-1])return false;
  if(now-issued_<1000||pending_||(boot_==boot&&v[1]==last_id_))return false;
  version_=version;mode_=version==2?v[2]:0u;
  boot_=boot;last_id_=pending_=v[1];issued_=now;return true;
 }
 uint32_t take(int64_t now,uint32_t boot,bool allowed,bool tx_idle){
  if(!clock(now))return 0;
  if(!allowed||boot!=boot_||now-issued_>1000){pending_=0;return 0;}
  if(!tx_idle)return 0;
  const auto id=pending_;pending_=0;return id; // Consume before even a short/failed write; never replay.
 }
};
inline size_t usb_diagnostic_prefix(char (&line)[192],uint32_t boot,uint32_t request,uint32_t seq,uint32_t rendered,const Sampler& sampler,const Swipe& swipe,bool eligible,unsigned blocked){
 const auto& s=sampler.stats();
 const uint32_t values[]={boot,request,seq,rendered,sampler.halted()?9u:sampler.stage(),s.setupFailures,s.ioErrors,s.invalid,s.gapMs,s.invalidCount,s.invalidX,s.invalidY,s.contacts,s.releases,swipe.armed()?1u:0u,swipe.tracking()?1u:0u,eligible?1u:0u,swipe.emitted(),blocked&255u,s.maxGapMs};
 std::memcpy(line,"TD1 ",4);size_t used=4;
 for(auto value:values){const int n=std::snprintf(line+used,sizeof(line)-used,"%lu ",static_cast<unsigned long>(value));if(n<=0||static_cast<size_t>(n)>=sizeof(line)-used)return 0;used+=n;}
 return used;
}
inline size_t usb_diagnostic_finish(char (&line)[192],size_t used){
 if(!used||used>=sizeof(line))return 0;
 const int n=std::snprintf(line+used,sizeof(line)-used,"%lu\n",static_cast<unsigned long>(watcher_wire::crc(line,used)));
 return n>0&&static_cast<size_t>(n)<sizeof(line)-used?used+n:0;
}
inline size_t usb_diagnostic_line(char (&line)[192],uint32_t boot,uint32_t request,uint32_t seq,uint32_t rendered,const Sampler& sampler,const Swipe& swipe,bool eligible,unsigned blocked){
 return usb_diagnostic_finish(line,usb_diagnostic_prefix(line,boot,request,seq,rendered,sampler,swipe,eligible,blocked));
}
} // namespace watcher_touch
