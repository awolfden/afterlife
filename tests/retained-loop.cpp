// Actual scene and new service includes with modeled IO, not hardware timing.
#define WATCHER_HAPTICS 1
#define WATCHER_RETAINED_PAGING 1
#define WATCHER_MOTION_METRICS 1
#include "../firmware/pet/main/ghost-wire.h"
#include "../firmware/pet/main/swipe.h"
#include "../firmware/render/motion-pager.h"
#include "../firmware/render/focus-transition.h"
#include "../firmware/render/retained-display.h"
#include "../firmware/render/render-authority.h"
#ifdef WATCHER_AFTERLIFE
#include "../firmware/pet/main/desk-runtime.h"
#include "../firmware/render/settings-scene.h"
static watcher_settings::Runtime desk;
#endif
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#define CHECK(e) do{if(!(e)){std::cerr<<__LINE__<<": " #e "\n";std::abort();}}while(false)
static int64_t clock_us=10000000;
static int64_t esp_timer_get_time(){return clock_us++;}
static int64_t ms(){return clock_us/1000;}
static unsigned reports=0;
static int usb_serial_jtag_write_bytes(const char* line,int size,int ticks){CHECK(size>0&&size<192&&ticks==0);++reports;std::cout.write(line,size);return size;}
constexpr uint16_t background=0x0842;
static watcher_wire::Frame frame{};
static uint32_t boot_id=7,displayed_seq=0,displayed_selected=0,displayed_count=0,displayed_rotation=0,paging_event=0;
static int paging_direction=0;static int64_t last_frame_ms=0;static bool ever_linked=true,displayed_preview=false,exit_requested=false;
static unsigned rotation=0,bounded_rotation=0,brightness=160,link_generation=1;
enum class LinkSource {none,usb,lan};static LinkSource link_source=LinkSource::usb;
static watcher_ghost::Page ghost_page;static watcher_ghost::FocusContext focus_context;
static watcher_render::FocusTransition focus_transition;static watcher_render::MotionPager carousel;static watcher_touch::Swipe swipe;
struct Audio {bool ready()const{return true;}bool fault()const{return false;}} audio;
static bool bounded_fault=false,bounded_brightness=false;
static unsigned revocations=0;
static void revoke(void*) noexcept {++revocations;}
static watcher_render::RenderAuthority bounded_authority(revoke,nullptr);
static watcher_render::RenderIdentity bounded_identity(int64_t now){const bool linked=frame.seq&&now>=last_frame_ms&&now-last_frame_ms<=1500;return {boot_id,static_cast<unsigned>(link_source),link_generation,frame.selected,frame.count,bounded_rotation,focus_context.boot==boot_id?focus_context.roster:0,focus_context.boot==boot_id?focus_context.selection:0,linked
#ifdef WATCHER_AFTERLIFE
 ,desk.generation()
#endif
 };}
static void bounded_observe(int64_t now){bounded_authority.observe(bounded_identity(now),frame.seq);displayed_seq=bounded_authority.shown();}
static void bounded_forget(int64_t now){auto key=bounded_identity(now);key.linked=false;bounded_authority.observe(key,0);bounded_authority.invalidate();displayed_seq=bounded_authority.shown();}
struct Bus {int64_t until=0,duration=24;bool stuck=false;unsigned floor=0;bool busy()const{return stuck||clock_us<until;}bool reserveDMABuffer(unsigned bytes){floor=bytes;return true;}void submit(){until=clock_us+duration;}} bus;
struct Framebuffer {
 struct Config {unsigned offset_rotation=0;} cfg;bool automatic=true;unsigned started=0,flushes=0;bool dirty=false;
 Bus* getBus()const{return &bus;}bool getAutoDisplay()const{return automatic;}void setAutoDisplay(bool b){automatic=b;}
 unsigned getStartCount()const{return started;}unsigned getWriteDepth()const{return 16;}const Config& config()const{return cfg;}
 void display(unsigned x,unsigned y,unsigned w,unsigned h){CHECK(x==0&&y==0&&w==0&&h==0&&!automatic&&!started&&!bus.busy()&&bus.floor==936);CHECK(dirty);++flushes;dirty=false;clock_us+=5000;bus.submit();}
} framebuffer;
struct Display {
 unsigned applied=0,brightness_calls=0,full_clears=0;int32_t x=0,y=0,w=468,h=468;bool expire=false;
 Framebuffer* getPanel()const{return &framebuffer;}int width()const{return 468;}int height()const{return 468;}unsigned getColorDepth()const{return 16;}unsigned getRotation()const{return applied;}
 void setRotation(unsigned r){CHECK(!bus.busy());applied=r;}
 void setBrightness(unsigned){CHECK(!bus.busy());++brightness_calls;bus.submit();}
 void getClipRect(int32_t* a,int32_t* b,int32_t* c,int32_t* d){*a=x;*b=y;*c=w;*d=h;}
 void setClipRect(int32_t a,int32_t b,int32_t c,int32_t d){x=a;y=b;w=c;h=d;}
 void startWrite(){CHECK(!bus.busy()&&!framebuffer.started);++framebuffer.started;}
 void endWrite(){CHECK(framebuffer.started==1);--framebuffer.started;}
 void fillRect(int,int,int,int,uint16_t){CHECK(!bus.busy());framebuffer.dirty=true;++clock_us;if(expire){expire=false;clock_us+=1600000;}}
 void fillScreen(uint16_t c){++full_clears;fillRect(0,0,468,468,c);}
 void fillEllipse(int,int,int,int,uint16_t c){fillRect(0,0,1,1,c);}
 void drawCircle(int,int,int,uint16_t c){fillRect(0,0,1,1,c);}void fillCircle(int,int,int,uint16_t c){fillRect(0,0,1,1,c);}
} display;
static void text(const char*,int,float,uint16_t c,uint16_t=background){display.fillRect(0,0,1,1,c);}
struct Clock {int64_t operator()()const{return esp_timer_get_time();}} bounded_clock;
static watcher_render::RetainedDisplay<Display,Framebuffer,Bus,Clock> bounded_display(display,bus,bounded_clock);
static watcher_render::SpriteRows motion_sprites;static watcher_render::RetainedFrame motion_frame;static watcher_render::SceneProgram bounded_program;
#include "../firmware/pet/main/bounded-scene.inc"
#include "../firmware/pet/main/motion-loop.inc"
#ifdef WATCHER_GHOST_FAMILY
static unsigned family_character_offset=0;
#endif
static void rows(){ghost_page={};focus_context={boot_id,frame.seq,5,6};for(unsigned i=0;i<frame.count;++i){watcher_ghost::Row row;row.boot=boot_id;row.seq=frame.seq;row.index=i;row.state=i%8;row.hue=i*21;row.rhythm=i%4;row.space_count=i+1;
#ifdef WATCHER_GHOST_FAMILY
row.family=true;row.character=(i+family_character_offset)%6;
#endif
snprintf(row.title,sizeof(row.title),"Session %u",i);ghost_page.accept(row,frame,ms());}}
static void refresh(){++frame.seq;last_frame_ms=ms();rows();bounded_observe(ms());}
static void tick(){clock_us=(clock_us/10000+1)*10000;}
int main(){
 CHECK(motion_sprites.initialize());CHECK(bounded_display.initialize(framebuffer,background)&&bounded_display.finish_band());
 frame.boot=boot_id;frame.count=3;frame.seq=10;frame.state=2;frame.flags=8;last_frame_ms=ms();rows();int64_t rendered=-1000;
 const auto boot_flushes=framebuffer.flushes;
 bounded_render(ms(),0,rendered);CHECK(displayed_seq==10&&!bounded_authority.active()&&!bounded_fault);CHECK(framebuffer.flushes==boot_flushes+1&&motion_metrics.frames==1);
 refresh();bounded_render(ms(),0,rendered);CHECK(displayed_seq==11&&framebuffer.flushes==boot_flushes+1&&motion_metrics.reused==1);
 // One whole frame may defer its final DMA row. Never one defer per band.
 bus.duration=2000;frame.state=3;refresh();const auto requested=frame.seq;
 bounded_render(ms(),0,rendered);CHECK(bounded_display.pending()&&bounded_authority.active()&&displayed_seq!=requested);
 const auto pending_flushes=framebuffer.flushes;refresh();bounded_render(ms(),0,rendered);CHECK(framebuffer.flushes==pending_flushes&&bounded_authority.active());
 tick();bounded_render(ms(),0,rendered);CHECK(displayed_seq==requested&&!bounded_authority.active()&&frame.seq>requested);
 bus.duration=24;
 // All four rotations must force a fresh retained scene AND clean panel padding.
 for(unsigned r=0;r<4;++r){const auto clears=display.full_clears;bounded_rotation=r;bounded_observe(ms());refresh();bounded_render(ms(),0,rendered);CHECK(rotation==r&&displayed_rotation==r&&!bounded_fault);if(r)CHECK(display.full_clears==clears+1);}
 // RAM expiry is discarded without an SPI flush; reconnect can render normally.
 frame.state=4;refresh();const auto before=framebuffer.flushes,discarded=motion_metrics.discarded;display.expire=true;
 bounded_render(ms(),0,rendered);CHECK(!bounded_fault&&framebuffer.flushes==before&&motion_metrics.discarded==discarded+1&&!framebuffer.started);
 refresh();bounded_render(ms(),0,rendered);CHECK(!bounded_fault&&displayed_seq==frame.seq&&framebuffer.flushes==before+1);
 // Pending identity ABA is sticky; never publish its old snapshot.
 bus.duration=2000;frame.state=5;refresh();bounded_render(ms(),0,rendered);CHECK(bounded_display.pending());
 frame.selected=1;bounded_observe(ms());frame.selected=0;bounded_observe(ms());CHECK(bounded_authority.invalid());
 tick();bus.duration=24;refresh();bounded_render(ms(),0,rendered);CHECK(!bounded_fault&&!bounded_authority.invalid()&&displayed_seq==frame.seq);
 // Newer event/ACK must not be cleared by the older completing snapshot.
 bus.duration=2000;frame.state=6;paging_event=10;frame.event_ack=10;refresh();bounded_render(ms(),0,rendered);CHECK(bounded_display.pending());paging_event=11;frame.event_ack=11;
 tick();bounded_render(ms(),0,rendered);CHECK(paging_event==11);bus.duration=24;refresh();bounded_render(ms(),0,rendered);CHECK(paging_event==0);
 bounded_brightness=true;bounded_render(ms(),0,rendered);CHECK(bounded_display.pending()&&!bounded_brightness);tick();bounded_render(ms(),0,rendered);CHECK(!bounded_fault);
 // Actual nonzero cadence: no early frames, immediate 50->16ms shortening,
 // exactly one newest frame after an overrun, and no catch-up burst.
 motion_due=0;refresh();bounded_render(ms(),50,rendered);const auto initial_start=motion_metrics.last_started;
 CHECK(motion_due==initial_start+50000);clock_us=motion_due-1;bounded_render(ms(),50,rendered);CHECK(motion_metrics.last_started==initial_start);
 clock_us=motion_due;bounded_render(ms(),50,rendered);const auto second_start=motion_metrics.last_started;CHECK(second_start==initial_start+50000);
 clock_us=second_start+16000;bounded_render(ms(),16,rendered);CHECK(motion_metrics.last_started==second_start+16000&&motion_due==motion_metrics.last_started+16000);
 clock_us=motion_metrics.last_started+100000;const auto published=motion_metrics.frames+motion_metrics.reused;bounded_render(ms(),16,rendered);
 CHECK(motion_metrics.frames+motion_metrics.reused==published+1&&motion_due==motion_metrics.last_started+16000);
 const auto late_start=motion_metrics.last_started;bounded_render(ms(),16,rendered);CHECK(motion_metrics.last_started==late_start);
 // Actual Swipe + scene + new service: held contacts never select or emit a
 // navigation; a confirmed release previews, and only a host frame selects.
 bounded_rotation=0;frame.count=3;frame.selected=0;frame.flags=0;frame.state=2;refresh();bounded_render(ms(),0,rendered);swipe={};
 unsigned events=0;
 for(unsigned i=0;i<9;++i){
  clock_us+=20000;
  const watcher_touch::Context context{true,boot_id,displayed_seq,0,3,0,1,link_generation,5,6};
  const watcher_touch::Sample sample=i<2||i>=7?watcher_touch::Sample{watcher_touch::Kind::released,0,0}:watcher_touch::Sample{watcher_touch::Kind::contact,170-static_cast<int>(i-2)*20,100};
  const auto gesture=swipe.update(sample,context,ms());
  if(gesture.kind){++events;CHECK(i==8&&gesture.kind==2);paging_event=200;paging_direction=1;}
  bounded_render(ms(),16,rendered);CHECK(frame.selected==0&&displayed_selected==0&&!bounded_fault);
  if(i>=3&&i<7)CHECK(displayed_preview&&carousel.previewing());
 }
 CHECK(events==1&&paging_event==200&&displayed_preview);
 for(unsigned i=0;i<5;++i){clock_us+=20000;bounded_render(ms(),16,rendered);CHECK(displayed_selected==0&&displayed_preview);}
 frame.selected=1;frame.event_ack=200;refresh();clock_us+=20000;bounded_render(ms(),16,rendered);CHECK(displayed_selected==1&&paging_event==0);
 for(unsigned i=0;i<12;++i){clock_us+=20000;bounded_render(ms(),16,rendered);}
 CHECK(!displayed_preview&&!carousel.previewing()&&frame.selected==1);
 frame.flags=8;
 // Broad integration over current host metadata, without the legacy raster engine.
 unsigned scenes=0;
 for(unsigned count=0;count<=16;++count)for(unsigned selected=0;selected<(count?count:1);++selected){frame.count=count;frame.selected=selected;frame.state=scenes%8;refresh();bounded_render(ms(),0,rendered);CHECK(!bounded_fault&&displayed_seq==frame.seq&&displayed_selected==selected&&displayed_count==count);++scenes;}
#ifdef WATCHER_GHOST_FAMILY
 // All six retain their character in solo mode and across link loss. The last
 // published costume is presentation only; expiry still revokes all authority.
 frame.count=1;frame.selected=0;frame.state=0;
 for(unsigned c=0;c<6;++c){family_character_offset=c;refresh();bounded_render(ms(),0,rendered);CHECK(last_published_character==static_cast<int>(c));CHECK(bounded_program.family_selection==static_cast<int>(c));}
 bus.duration=2000;family_character_offset=1;refresh();bounded_render(ms(),0,rendered);CHECK(bounded_display.pending()&&last_published_character==5);
 family_character_offset=2;refresh();tick();bounded_render(ms(),0,rendered);CHECK(last_published_character==1); // Completed old owned scene, never relabel with the newer heartbeat.
 bus.duration=24;family_character_offset=5;refresh();bounded_render(ms(),0,rendered);CHECK(last_published_character==5);
 last_frame_ms=ms()-1600;bounded_render(ms(),0,rendered);CHECK(!displayed_seq&&!bounded_authority.completed().identity.linked&&last_published_character==5);
#ifdef WATCHER_AFTERLIFE
 CHECK(bounded_program.count()==3&&bounded_program.op(0).kind==watcher_render::SceneKind::afterlife);
 CHECK(bounded_program.op(1).kind==watcher_render::SceneKind::text&&std::string(bounded_program.text_at(0))=="Connect to Pi");
 CHECK(bounded_program.family_selection==-1&&bounded_program.op(1).text_background==0x1063);
 const auto offline_flushes=framebuffer.flushes;clock_us+=20000;bounded_render(ms(),0,rendered);CHECK(framebuffer.flushes==offline_flushes&&!displayed_seq);
 ever_linked=false;bounded_render(ms(),0,rendered);CHECK(bounded_program.op(0).kind==watcher_render::SceneKind::afterlife&&framebuffer.flushes==offline_flushes);ever_linked=true;
#else
 bool offline_costume=false;for(unsigned i=0;i<bounded_program.count();++i){const auto& op=bounded_program.op(i);if(op.kind==watcher_render::SceneKind::ghost)offline_costume=op.family&&op.character==5&&op.state==8;}
 CHECK(offline_costume);
#endif
 family_character_offset=0;
#endif
#ifdef WATCHER_AFTERLIFE
 // Host is fresh but sessions have not registered. Loading is presentation
 // only, never a session selection; it must not resurrect the old empty UI.
 frame.count=0;frame.selected=0;frame.state=9;frame.flags=0;
 for(unsigned i=0;i<48;++i){clock_us+=33000;refresh();bounded_render(ms(),0,rendered);
  CHECK(!bounded_fault&&displayed_seq==frame.seq&&displayed_count==0&&!displayed_preview);
  CHECK(bounded_program.count()==4&&bounded_program.op(0).kind==watcher_render::SceneKind::afterlife&&bounded_program.family_selection==-1);
  CHECK(std::string(bounded_program.text_at(0)).size()==19&&std::string(bounded_program.text_at(0)).find("Connecting to Pi")==0&&bounded_program.op(1).text_background==bounded_program.op(1).color);
  const auto& loader=bounded_program.op(2);CHECK(loader.kind==watcher_render::SceneKind::ghost&&loader.family&&loader.character==0&&loader.scale==32&&loader.state==0);
  const auto loader_bounds=watcher_render::paint_bounds(loader);CHECK(loader_bounds.y>=360&&loader_bounds.bottom<=watcher_settings::entry_rect.y);
 }
 frame.flags=8;refresh();bounded_render(ms(),0,rendered);const auto reduced_flushes=framebuffer.flushes;
 for(unsigned i=0;i<4;++i){clock_us+=600000;refresh();bounded_render(ms(),0,rendered);CHECK(framebuffer.flushes==reduced_flushes&&bounded_program.op(2).x==233&&bounded_program.op(2).y==381&&!bounded_program.op(2).blink);}
 last_frame_ms=ms()-1600;bounded_render(ms(),0,rendered);CHECK(!displayed_seq&&bounded_program.count()==3&&std::string(bounded_program.text_at(0))=="Connect to Pi");
 // Offline entry is local publication, never fabricated session authority.
 for(auto kind:{watcher_touch::Kind::released,watcher_touch::Kind::released,watcher_touch::Kind::contact,watcher_touch::Kind::released,watcher_touch::Kind::released}){
  clock_us+=20000;desk.context(boot_id,frame.seq,ms(),false);desk.touch({kind,rotation==0?116:rotation==1?20:rotation==2?117:213,rotation==0?213:rotation==1?116:rotation==2?20:117},bounded_authority.local_touch_allowed(bounded_identity(ms())),rotation);
 }
 CHECK(desk.open()&&!bounded_authority.local_touch_allowed(bounded_identity(ms())));
 bounded_render(ms(),0,rendered);CHECK(!bounded_fault&&!displayed_seq&&bounded_authority.completed().local&&bounded_authority.local_touch_allowed(bounded_identity(ms())));
 CHECK(std::string(bounded_program.text_at(0))=="Settings");
 CHECK(desk.back());bounded_render(ms(),0,rendered);CHECK(!displayed_seq&&!bounded_authority.completed().local);
 frame.count=1;frame.state=7;refresh();bounded_render(ms(),0,rendered);CHECK(bounded_program.op(0).kind!=watcher_render::SceneKind::afterlife&&displayed_count==1);
#endif
 // Link/exit drain: no new writes after exit, old authority revoked first.
 frame.count=3;frame.selected=0;frame.state=2;bus.duration=2000;refresh();bounded_render(ms(),0,rendered);CHECK(bounded_display.pending());const auto exiting=framebuffer.flushes;exit_requested=true;
 bounded_render(ms(),0,rendered);CHECK(!displayed_seq&&bounded_display.pending());tick();bounded_render(ms(),0,rendered);CHECK(!bounded_authority.active()&&framebuffer.flushes==exiting);exit_requested=false;
 bus.duration=24;refresh();bounded_render(ms(),0,rendered);CHECK(displayed_seq&&revocations>0);
 motion_report(ms());CHECK(reports==1);motion_report(ms());CHECK(reports==1);
 // Completion timeout is terminal even if the bus subsequently becomes idle.
 bus.duration=2000;frame.state=7;refresh();bounded_render(ms(),0,rendered);CHECK(bounded_display.pending());bus.stuck=true;clock_us+=51000;bounded_render(ms(),0,rendered);CHECK(bounded_fault&&!displayed_seq);const auto failed=framebuffer.flushes;bus.stuck=false;tick();refresh();bounded_render(ms(),0,rendered);CHECK(framebuffer.flushes==failed&&!displayed_seq);
 std::cout<<"retained loop: "<<scenes<<" roster scenes; single-flush/no-copy lifecycle, no-op publication, deferred DMA, all rotations, expiry recovery, ABA, snapshot ACK, brightness, exit drain and terminal timeout pass (modeled IO)\n";
}
