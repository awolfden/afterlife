// Compile the ACTUAL preparation/service includes against deterministic fake IO.
// Primitive/text call equivalence is not a claim about M5GFX font pixels.
#define WATCHER_GHOST_PAGES 1
#define WATCHER_TOUCH_SWIPE 1
#define WATCHER_TOUCH_MEASURED_SCALE 1
#define WATCHER_AUTO_ROTATE 1
#define WATCHER_LAN 1
#define WATCHER_HAPTICS 1
#include "../firmware/pet/main/ghost-wire.h"
#include "../firmware/pet/main/touch.h"
#ifdef WATCHER_TOUCH_DIAGNOSTICS
#include "../firmware/pet/main/touch-diagnostic.h"
static watcher_touch::Sampler touch_sampler;static bool touch_eligible=true;static unsigned touch_blocked=0;
#endif
#include "../firmware/render/scene-job.h"
#include "../firmware/render/focus-transition.h"
#include "../firmware/render/display-transfer.h"
#include "../firmware/render/render-authority.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#define CHECK(e) do{if(!(e)){std::cerr<<__LINE__<<": " #e "\n";std::abort();}}while(false)
namespace lgfx {namespace fonts {static int Font0=0;}}
static int64_t clock_us=10000000;
static int64_t esp_timer_get_time(){return clock_us++;}
static int64_t ms(){return clock_us/1000;}
constexpr uint16_t background=0x0842;
static watcher_wire::Frame frame{};
static uint32_t boot_id=7,displayed_seq=0,displayed_selected=0,displayed_count=0,displayed_rotation=0,paging_event=0;
static int paging_direction=0;static int64_t last_frame_ms=0;static bool ever_linked=true,displayed_preview=false,exit_requested=false;
static unsigned rotation=0,bounded_rotation=0,brightness=160,link_generation=1;
enum class LinkSource {none,usb,lan};static LinkSource link_source=LinkSource::usb;
static watcher_ghost::Page ghost_page;static watcher_ghost::FocusContext focus_context;
static watcher_render::FocusTransition focus_transition;static watcher_render::Carousel carousel;static watcher_touch::Swipe swipe;
struct Audio {bool ready()const{return true;}bool fault()const{return false;}} audio;
static bool bounded_fault=false,bounded_brightness=false;
static unsigned revocations=0;
static void revoke(void*) noexcept {++revocations;}
static watcher_render::RenderAuthority bounded_authority(revoke,nullptr);
static watcher_render::RenderIdentity bounded_identity(int64_t now){const bool linked=frame.seq&&now>=last_frame_ms&&now-last_frame_ms<=1500;return {boot_id,static_cast<unsigned>(link_source),link_generation,frame.selected,frame.count,bounded_rotation,focus_context.boot==boot_id?focus_context.roster:0,focus_context.boot==boot_id?focus_context.selection:0,linked};}
static void bounded_observe(int64_t now){bounded_authority.observe(bounded_identity(now),frame.seq);displayed_seq=bounded_authority.shown();}
static void bounded_forget(int64_t now){auto key=bounded_identity(now);key.linked=false;bounded_authority.observe(key,0);bounded_authority.invalidate();displayed_seq=bounded_authority.shown();}
struct Bus {
 int64_t until=0,duration=24;bool stuck=false;unsigned floor=0;
 bool busy()const{return stuck||clock_us<until;}
 bool reserveDMABuffer(unsigned bytes){floor=bytes;return true;}
 void submit(){until=clock_us+duration;}
} bus;
struct Framebuffer {
 struct Config {unsigned offset_rotation=0;} cfg;
 Bus* getBus()const{return &bus;}bool getAutoDisplay()const{return true;}
 unsigned getStartCount()const{return 0;}unsigned getWriteDepth()const{return 16;}
 const Config& config()const{return cfg;}
} framebuffer;
struct Display {
 unsigned applied=0,brightness_calls=0;int32_t x=0,y=0,w=468,h=468;
 Framebuffer* getPanel()const{return &framebuffer;}int width()const{return 468;}int height()const{return 468;}
 unsigned getColorDepth()const{return 16;}unsigned getRotation()const{return applied;}
 void setRotation(unsigned r){CHECK(!bus.busy());applied=r;}
 void setBrightness(unsigned){CHECK(!bus.busy());++brightness_calls;bus.submit();}
 void getClipRect(int32_t* a,int32_t* b,int32_t* c,int32_t* d){*a=x;*b=y;*c=w;*d=h;}
 void setClipRect(int32_t a,int32_t b,int32_t c,int32_t d){x=a;y=b;w=c;h=d;}
 void fillScreen(uint16_t){CHECK(bus.floor==936);bus.submit();}
} display;
struct Canvas {
 std::vector<uint16_t> pixels=std::vector<uint16_t>(466*466);
 std::vector<std::string> calls;unsigned spans=0,pushes=0,legacy_pushes=0;float text_size=1;uint16_t text_color=0;
 int width()const{return 466;}int height()const{return 466;}unsigned getColorDepth()const{return 16;}void* getBuffer(){return pixels.data();}
 void reset(){calls.clear();spans=legacy_pushes=0;}
 void fillRect(int x,int y,int w,int h,uint16_t color){CHECK(!bus.busy());CHECK(x>=0&&y>=0&&w>=0&&h>=0&&x+w<=466&&y+h<=466);for(int row=y;row<y+h;++row)std::fill(pixels.begin()+row*466+x,pixels.begin()+row*466+x+w,color);if(w!=466)++spans;clock_us+=1+w*h/64;}
 void fillScreen(uint16_t color){fillRect(0,0,466,466,color);}
 void fillEllipse(int x,int y,int a,int b,uint16_t c){calls.push_back("ellipse:"+std::to_string(x)+":"+std::to_string(y)+":"+std::to_string(a)+":"+std::to_string(b)+":"+std::to_string(c)+"@"+std::to_string(spans));clock_us+=30;}
 void circle(int x,int y,int r,uint16_t c,bool fill){calls.push_back("circle:"+std::to_string(x)+":"+std::to_string(y)+":"+std::to_string(r)+":"+std::to_string(c)+":"+std::to_string(fill)+"@"+std::to_string(spans));clock_us+=10;}
 void drawCircle(int x,int y,int r,uint16_t c){circle(x,y,r,c,false);}void fillCircle(int x,int y,int r,uint16_t c){circle(x,y,r,c,true);}
 void setTextSize(float size){text_size=size;}void setTextColor(uint16_t color,uint16_t bg){CHECK(bg==background);text_color=color;}
 void drawCenterString(const char* s,int x,int y,const int*){CHECK(x==233);calls.push_back("text:"+std::string(s)+":"+std::to_string(y)+":"+std::to_string(text_size)+":"+std::to_string(text_color)+"@"+std::to_string(spans));clock_us+=100;}
 void pushSprite(int,int){++legacy_pushes;} // Reference publication only, no simulated bus.
 void pushSprite(Display* target,int x,int y){CHECK(!bus.busy()&&target==&display&&x==0&&y==0);++pushes;clock_us+=250;bus.submit();}
} canvas;
static void text(const char* value,int y,float size,uint16_t color){canvas.setTextSize(size);canvas.setTextColor(color,background);canvas.drawCenterString(value,233,y,&lgfx::fonts::Font0);}
struct Clock {int64_t operator()()const{return esp_timer_get_time();}} bounded_clock;
static watcher_render::DisplayTransfer<Display,Canvas,Framebuffer,Bus,Clock> bounded_display(display,canvas,bus,bounded_clock,16);
static watcher_render::SceneProgram bounded_program;static watcher_render::SceneJob bounded_scene;static watcher_render::TransferJob bounded_transfer;static bool bounded_transferring=false;
#include "fixtures/legacy-render.inc"
#define prepare_bounded_scene prepare_actual_scene
#include "../firmware/pet/main/bounded-scene.inc"
#undef prepare_bounded_scene
static bool inject_invalid_scene=false;
static bool prepare_bounded_scene(int64_t now,watcher_render::SceneProgram& program,watcher_render::RenderSnapshot& snapshot){
 const bool ok=prepare_actual_scene(now,program,snapshot);
 if(ok&&inject_invalid_scene){const std::string oversized(64,'x');program.text(oversized.c_str(),410,1,0);}
 return ok&&program.valid();
}
#include "../firmware/pet/main/bounded-loop.inc"
static void rows(){
 ghost_page={};focus_context={boot_id,frame.seq,5,6};
 for(unsigned i=0;i<frame.count;++i){watcher_ghost::Row row;row.boot=boot_id;row.seq=frame.seq;row.index=i;row.state=i%8;row.pending=row.unseen=i%2;row.hue=i*21;row.rhythm=i%4;row.space_count=i+1;snprintf(row.title,sizeof(row.title),"Owned title %u",i);if(i&1u){for(unsigned n=0;n<32;n+=2){row.title[n]=static_cast<char>(0xc3);row.title[n+1]=static_cast<char>(0xa9);}row.title[32]=0;}ghost_page.accept(row,frame,ms());}
}
static void setup(unsigned count,unsigned selected,unsigned state,unsigned flags){
 frame={};frame.boot=boot_id;frame.seq=10;frame.count=count;frame.selected=selected;frame.state=state%8;frame.flags=flags;frame.pending=3;frame.other=2;frame.event_ack=1;
 last_frame_ms=ms();ever_linked=state!=9;if(state>=8)last_frame_ms-=2000;
 focus_transition={};carousel={};swipe={};paging_event=0;rows();
}
static void yield_tick(){clock_us=(clock_us/10000+1)*10000;}
static void refresh(){++frame.seq;last_frame_ms=ms();rows();bounded_observe(ms());}
int main(int argc,char** argv){
 if(argc>1){
  CHECK(argc==2&&std::strcmp(argv[1],"--invalid-scene")==0);
  setup(3,0,2,0);CHECK(bounded_display.initialize(framebuffer,background)&&bounded_display.finish_band());
  // Inject a hypothetical producer overflow after actual scene preparation.
  inject_invalid_scene=true;int64_t rendered=-1000;
  bounded_render(ms(),0,rendered);CHECK(bounded_fault&&!bounded_program.valid()&&!bounded_display.faulted()&&!canvas.pushes&&!displayed_seq);
  inject_invalid_scene=false;bounded_render(ms(),0,rendered);CHECK(bounded_fault&&!canvas.pushes&&!displayed_seq);
  std::cout<<"invalid scene: RAM-only producer rejection remains terminal without pixel submission\n";return 0;
 }
 unsigned comparisons=0;
 for(unsigned count=0;count<=16;++count)for(unsigned selected=0;selected<(count?count:1);++selected)for(unsigned state=0;state<10;++state)for(unsigned flags:{0u,2u,8u,21u}){
  setup(count,selected,state,flags);const auto now=ms();const auto focus=focus_transition;const auto ring=carousel;
  canvas.reset();render(now);CHECK(canvas.legacy_pushes==1);const auto pixels=canvas.pixels;const auto calls=canvas.calls;const bool preview=displayed_preview;
  focus_transition=focus;carousel=ring;canvas.reset();watcher_render::SceneProgram program;watcher_render::RenderSnapshot snapshot;
  CHECK(prepare_bounded_scene(now,program,snapshot));CHECK(snapshot.preview==preview);
  watcher_render::SceneJob job;CHECK(job.begin(program));while(!job.complete())job.step(17,bounded_surface);
  CHECK(canvas.pixels==pixels&&canvas.calls==calls);++comparisons;
 }
 // Metadata wait preserves pixels: the actual preparation declines a new scene.
 setup(3,0,2,0);++frame.seq;watcher_render::SceneProgram waiting;watcher_render::RenderSnapshot ignored;CHECK(!prepare_bounded_scene(ms(),waiting,ignored));
 // Real service includes + real controllers + pinned-API fake driver.
 setup(3,0,2,0);CHECK(bounded_display.initialize(framebuffer,background));CHECK(bounded_display.finish_band());
 int64_t rendered=-1000;unsigned ticks=0;const auto original_seq=frame.seq;
 while(!bounded_authority.shown()){
  CHECK(++ticks<100);bounded_render(ms(),20,rendered);yield_tick();refresh();
 }
 CHECK(bounded_authority.shown()==original_seq&&frame.seq>original_seq); // Heartbeats never relabel the job.
 // Rotation arrives during a band whose DMA does NOT finish in its first fence.
 bus.duration=2000;
 while(!bounded_transfer.pending()){CHECK(++ticks<200);bounded_render(ms(),20,rendered);if(!bounded_transfer.pending()){yield_tick();refresh();}}
 CHECK(bus.busy()&&bounded_authority.pixels_started());
 bounded_rotation=1;bounded_observe(ms());const auto old_pushes=canvas.pushes;
 bounded_render(ms(),20,rendered);CHECK(display.applied==0&&bounded_transfer.pending()&&!displayed_seq);
 bus.duration=24;yield_tick();refresh();
 for(unsigned i=0;i<100&&!(rotation==1&&bounded_authority.shown()&&bounded_authority.completed().identity.rotation==1);++i){bounded_render(ms(),0,rendered);yield_tick();refresh();}
 CHECK(!bounded_fault&&rotation==1&&display.applied==1&&bounded_authority.completed().identity.rotation==1&&canvas.pushes>old_pushes);
 // Brightness is queued through the adapter and does not relabel or clear jobs.
 bounded_brightness=true;const auto old_brightness=display.brightness_calls;
 for(unsigned i=0;i<30&&bounded_brightness;++i){bounded_render(ms(),0,rendered);yield_tick();refresh();}
 CHECK(!bounded_fault&&!bounded_brightness&&display.brightness_calls==old_brightness+1);
 // Real Sampler (including four setup stages + fixed measured decoder) and
 // Swipe, with synthetic1ms preceding service and2ms reads. No manual due test.
 swipe={};watcher_touch::Sampler sampler;auto began=ms();int64_t last_sample=-1;unsigned nav=0,contacts=0;uint64_t max_gap=0;
 for(unsigned i=0;i<70;++i){
  clock_us+=1000;const auto now=ms();
  watcher_touch::Context c{bounded_authority.touch_allowed(bounded_identity(now)),boot_id,displayed_seq,frame.selected,frame.count,rotation,static_cast<unsigned>(link_source),link_generation,5,6};
  swipe.expire(now);swipe.guard(c,now);watcher_touch::Sample sample;
  if(c.enabled&&sampler.poll(now,sample,[&](uint8_t reg,uint8_t* bytes,unsigned length){
   clock_us+=2000;std::memset(bytes,0,length);if(reg==0xa7){CHECK(length==3);return true;}
   CHECK(reg==0x02&&length==6);
   if(last_sample>=0)max_gap=std::max(max_gap,static_cast<uint64_t>(now-last_sample));last_sample=now;
   const auto age=now-began;const bool contact=age>=200&&age<320;
   if(contact){
    // At rotation1, measured raw(266,2*x) -> normalized(133,x)
    // -> displayed(x,100). A raw horizontal trace would be vertical.
    const unsigned x=266,y=2*(180-(age-200)*160/120);bytes[0]=1;bytes[1]=x>>8;bytes[2]=x&255;bytes[3]=y>>8;bytes[4]=y&255;++contacts;
   }
   return true;
  },[](uint8_t,uint8_t){return true;})){
   const auto event=swipe.update(sample,c,now);if(event.kind)++nav;
  }
  bounded_render(ms(),20,rendered);yield_tick();refresh();
 }
 CHECK(nav==1&&contacts>=3&&max_gap<100&&!bounded_fault);
 std::cout<<"synthetic schedule: contacts="<<contacts<<" max_gap_ms="<<max_gap<<" nav="<<nav<<" (not hardware timing)\n";
 // A retired RAM-only preview must never reach the display.
 auto drain_scene=[&](){for(unsigned i=0;bounded_authority.active();++i){CHECK(i<100);bounded_render(ms(),0,rendered);yield_tick();refresh();}};
 drain_scene();swipe={};const auto at=ms();
 watcher_touch::Context drag{true,boot_id,displayed_seq,frame.selected,frame.count,rotation,static_cast<unsigned>(link_source),link_generation,5,6};
 swipe.update({watcher_touch::Kind::released},drag,at-100);swipe.update({watcher_touch::Kind::released},drag,at-80);
 swipe.update({watcher_touch::Kind::contact,133,180},drag,at-60);swipe.update({watcher_touch::Kind::contact,133,130},drag,at-40);swipe.update({watcher_touch::Kind::contact,133,80},drag,at-20);
 CHECK(swipe.tracking());watcher_render::RenderSnapshot preview;
 CHECK(prepare_bounded_scene(at,bounded_program,preview)&&preview.preview);CHECK(bounded_authority.start(preview)&&bounded_scene.begin(bounded_program));bounded_transferring=false;
 const auto before_preview=canvas.pushes,before_revoke=revocations,old_shown=displayed_seq;
 bounded_authority.invalidate();
 CHECK(!bounded_authority.pixels_started()&&canvas.pushes==before_preview&&revocations==before_revoke&&displayed_seq==old_shown);
 drain_scene();swipe.cancel();focus_transition.reset();carousel.reset();
 // A newer ACK arriving while P1 is rasterizing must not let P1 clear P2.
 paging_event=1;frame.event_ack=1;refresh();watcher_render::RenderSnapshot p1;
 CHECK(prepare_bounded_scene(ms(),bounded_program,p1)&&p1.paging_event==1);CHECK(bounded_authority.start(p1)&&bounded_scene.begin(bounded_program));bounded_transferring=false;
 paging_event=2;frame.event_ack=2;refresh();drain_scene();CHECK(paging_event==2&&bounded_authority.completed().event_ack==1);
 for(unsigned i=0;paging_event;++i){CHECK(i<100);bounded_render(ms(),0,rendered);yield_tick();refresh();}
 CHECK(bounded_authority.completed().paging_event==2&&!bounded_fault);
 // Rotation during RAM-only work cancels before any band is submitted.
 drain_scene();watcher_render::RenderSnapshot ram_rotation;
 CHECK(prepare_bounded_scene(ms(),bounded_program,ram_rotation)&&bounded_authority.start(ram_rotation)&&bounded_scene.begin(bounded_program));bounded_transferring=false;
 const auto ram_pushes=canvas.pushes;bounded_rotation=2;bounded_observe(ms());bounded_render(ms(),0,rendered);
 CHECK(!bounded_authority.active()&&canvas.pushes==ram_pushes&&rotation==1&&!bounded_fault);
 for(unsigned i=0;rotation!=2||bounded_authority.completed().identity.rotation!=2;++i){CHECK(i<100);bounded_render(ms(),0,rendered);yield_tick();refresh();}
 // Mid-frame brightness is serialized against DMA, not whole-frame atomic.
 drain_scene();watcher_render::RenderSnapshot mid_brightness;
 CHECK(prepare_bounded_scene(ms(),bounded_program,mid_brightness)&&bounded_authority.start(mid_brightness)&&bounded_scene.begin(bounded_program));
 while(!bounded_scene.complete())bounded_scene.step(64,bounded_surface);
 CHECK(bounded_transfer.begin(rotation,16));bounded_transferring=true;CHECK(bounded_authority.begin_pixels(bounded_identity(ms()),frame.seq));
 bounded_transfer.step(bounded_display);CHECK(bounded_transfer.completed_bands()==1&&!bounded_transfer.pending());
 const auto brightness_before=display.brightness_calls;bounded_brightness=true;bounded_render(ms(),0,rendered);
 CHECK(!bounded_brightness&&display.brightness_calls==brightness_before+1&&bounded_transfer.completed_bands()>1&&!bounded_fault);
 drain_scene();CHECK(bounded_authority.completed().seq==mid_brightness.seq&&bounded_transfer.completed_bands()==bounded_transfer.submitted_bands());
 // Link expiry INSIDE the final band: physical completion must not publish a
 // stale snapshot. This seam advances fake time, not nested message servicing.
 drain_scene();watcher_render::RenderSnapshot expires;
 CHECK(prepare_bounded_scene(ms(),bounded_program,expires)&&bounded_authority.start(expires)&&bounded_scene.begin(bounded_program));
 while(!bounded_scene.complete())bounded_scene.step(64,bounded_surface);
 CHECK(bounded_transfer.begin(rotation,16));bounded_transferring=true;CHECK(bounded_authority.begin_pixels(bounded_identity(ms()),frame.seq));
 while(bounded_transfer.completed_bands()<29){bounded_transfer.step(bounded_display);CHECK(!bounded_transfer.pending());}
 clock_us=(clock_us/1000+1)*1000+800;last_frame_ms=ms()-1500;const auto expiry_pushes=canvas.pushes;
 CHECK(bounded_identity(ms()).linked);bounded_render(ms(),0,rendered);
 CHECK(canvas.pushes==expiry_pushes+1&&bounded_transfer.publishable()&&bounded_authority.invalid()&&!displayed_seq&&!bounded_fault);
 refresh();bounded_render(ms(),0,rendered);CHECK(!bounded_authority.active()&&!displayed_seq&&!bounded_fault);
 for(unsigned i=0;!displayed_seq;++i){CHECK(i<100);bounded_render(ms(),0,rendered);yield_tick();refresh();}
 // Exit with DMA pending: repeated service calls must only drain, never draw,
 // submit, publish or restart a scene. No simulated MCU restart is performed.
 drain_scene();bus.duration=2000;
 for(unsigned i=0;!bounded_transfer.pending();++i){CHECK(i<100);bounded_render(ms(),0,rendered);if(!bounded_transfer.pending()){yield_tick();refresh();}}
 const auto exit_pushes=canvas.pushes;exit_requested=true;
 for(unsigned i=0;i<3;++i){bounded_render(ms(),0,rendered);CHECK(bounded_transfer.pending()&&!displayed_seq&&canvas.pushes==exit_pushes);}
 yield_tick();bounded_render(ms(),0,rendered);
 CHECK(!bounded_authority.active()&&!bounded_transfer.pending()&&!bounded_display.pending()&&!bounded_fault);
 for(unsigned i=0;i<3;++i){bounded_render(ms(),0,rendered);CHECK(!bounded_authority.active()&&!displayed_seq&&canvas.pushes==exit_pushes);yield_tick();}
 exit_requested=false;bus.duration=24;refresh();
 for(unsigned i=0;!displayed_seq;++i){CHECK(i<100);bounded_render(ms(),0,rendered);yield_tick();refresh();}
 // Non-fault link retirement during pending DMA, followed by a new generation.
 drain_scene();bus.duration=2000;
 for(unsigned i=0;!bounded_transfer.pending();++i){CHECK(i<100);bounded_render(ms(),0,rendered);if(!bounded_transfer.pending()){yield_tick();refresh();}}
 const auto loss_pushes=canvas.pushes;bounded_forget(ms());frame.seq=0;link_source=LinkSource::none;++link_generation;
 for(unsigned i=0;i<3;++i){bounded_render(ms(),0,rendered);CHECK(bounded_transfer.pending()&&!displayed_seq&&canvas.pushes==loss_pushes);}
 yield_tick();bounded_render(ms(),0,rendered);
 CHECK(!bounded_authority.active()&&!bounded_transfer.pending()&&!bounded_fault&&canvas.pushes==loss_pushes);
 link_source=LinkSource::usb;bus.duration=24;frame.seq=10000;refresh();
 for(unsigned i=0;!displayed_seq;++i){CHECK(i<100);bounded_render(ms(),0,rendered);yield_tick();refresh();}
 CHECK(bounded_authority.completed().identity.generation==link_generation&&!bounded_fault);
 // Last-band/partial-transfer timeout is terminal; late idle cannot publish,
 // reuse the canvas or rotate an unknown frame.
 drain_scene();bus.duration=1000000;const auto before_fault=revocations;
 for(unsigned i=0;!bounded_fault;++i){CHECK(i<100);bounded_render(ms(),0,rendered);yield_tick();refresh();}
 CHECK(!displayed_seq&&revocations==before_fault+1&&bounded_transfer.pending());
 const auto failed_pushes=canvas.pushes;bus.until=clock_us;CHECK(!bounded_display.finish_band());bounded_render(ms(),0,rendered);
 CHECK(!displayed_seq&&canvas.pushes==failed_pushes&&!bounded_transfer.publishable());
 std::cout<<"bounded loop: "<<comparisons<<" frozen legacy scene comparisons; snapshot heartbeat, rotation, brightness and due-sample navigation pass\n";
}
