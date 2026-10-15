// Compiles the exact current main.cpp accept() body against fake side effects.
// Covers common USB/LAN acceptance, not transport arbitration or physical IO.
#define WATCHER_GHOST_PAGES 1
#define WATCHER_GHOST_FAMILY 1
#define WATCHER_BOUNDED_RENDER 1
#define WATCHER_COMPLETION_CHIME 1
#define WATCHER_FACTORY_FEEDBACK 1
#include "../firmware/pet/main/ghost-wire.h"
#include "../firmware/pet/main/completion-chime.h"
#ifdef WATCHER_AFTERLIFE
#include "../firmware/pet/main/desk-runtime.h"
static watcher_settings::Runtime desk;
#endif
#include <cassert>
#include <cstring>
#include <string>
#include <cstdio>
static int64_t now_ms=1000,last_frame_ms=900;
static int64_t ms(){return now_ms;}
static uint32_t boot_id=7,last_alert=0,button_id=4,button_kind=3,factory_event_boot=0;
static bool ever_linked=false;
static unsigned statuses=0,observations=0,expiries=0;
static watcher_wire::Frame frame{};
static watcher_ghost::Page ghost_page;
static watcher_ghost::FocusContext focus_context;
static watcher_chime::Gate chime_gate;
static void expire_link(int64_t now){assert(now==1000);++expiries;} // Fresh-link fixture only.
static void send_status(){++statuses;}
static void bounded_observe(int64_t){++observations;}
struct Output {unsigned starts=0,contexts=0,alerts=0;void start(int64_t,unsigned=3){++starts;}void context(int64_t,bool){++contexts;}void alert(const watcher_wire::Frame&,bool=true,unsigned=2){++alerts;}};
static Output audio;
#ifdef WATCHER_AFTERLIFE
static void desk_context(int64_t now){desk.context(boot_id,frame.seq,now,frame.seq&&now>=last_frame_ms&&now-last_frame_ms<=1500);}
#endif
#include "actual-accept.inc"
static std::string packet(const std::string& prefix){return prefix+std::to_string(watcher_wire::crc(prefix.data(),prefix.size()));}
int main(){
 for(bool linked:{false,true}){
  frame={};frame.boot=7;frame.seq=linked?10:0;frame.count=3;frame.selected=1;frame.state=2;
  last_frame_ms=900;ever_linked=linked;button_kind=3;last_alert=0;factory_event_boot=0;
  statuses=observations=expiries=0;audio={};
  unsigned char original[sizeof frame];std::memcpy(original,&frame,sizeof frame);
  // Valid predecessor offer and grant/cancel/status records, with correct CRC.
  const std::string offer(32,'1'),nonce(32,'2');
  for(const auto& prefix:{"V0 7 10 "+offer+" 1 ","V0 7 10 "+offer+" 0 ","V1 7 "+offer+" 1 "+nonce+" 1 ","V1 7 "+offer+" 1 "+nonce+" 4 ","V1 7 "+offer+" 1 "+nonce+" 9 "}){
   const auto line=packet(prefix);accept(line.data(),line.size());
   assert(!std::memcmp(original,&frame,sizeof frame));assert(last_frame_ms==900&&ever_linked==linked);
   assert(button_id==4&&button_kind==3&&last_alert==0&&!factory_event_boot);
   assert(!statuses&&!observations&&!audio.starts&&!audio.contexts&&!audio.alerts);
  }
  assert(expiries==5);
  // Ordinary metadata subsequently succeeds and retains the usual ACK/output dispatch.
  const auto good=packet("W1 7 11 1 3 2 0 0 0 0 4 ");accept(good.data(),good.size());
  assert(frame.seq==11&&frame.selected==1&&frame.count==3&&frame.event_ack==4);
  assert(last_frame_ms==1000&&ever_linked&&!button_kind&&statuses==1&&observations==1);
  assert(!audio.starts&&audio.contexts==1&&audio.alerts==1);
#ifdef WATCHER_AFTERLIFE
  const auto restore=packet("S1 7 11 11111111111111111111111111111111 1 0 0 5 160 0 ");accept(restore.data(),restore.size());
  assert(desk.sync().supported()&&desk.outputs()==2);assert(statuses==1&&frame.seq==11&&last_frame_ms==1000&&!audio.starts&&audio.alerts==1);
  const auto stale=packet("S1 7 10 22222222222222222222222222222222 1 0 0 7 240 0 ");accept(stale.data(),stale.size());
  assert(desk.sync().values()==(watcher_settings::Values{5,160})&&statuses==1&&!audio.starts&&audio.alerts==1);
  desk=watcher_settings::Runtime{};
#endif
 }
 std::puts("retired packets preserve authority; ordinary metadata still accepted");
}
