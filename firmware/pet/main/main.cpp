// Original pet application. Explicit hardware setup; never M5.begin()/board autodetect.
// StopWatch panel command sequence/configuration adapted from MIT M5GFX 0.2.26.
#include <M5GFX.h>
#include <lgfx/v1/panel/Panel_CO5300.hpp>
#include "esp_rom_sys.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wire.h"
#include "buttons.h"
#ifdef WATCHER_AFTERLIFE
#include "desk-runtime.h"
#include "status-route.h"
#include "../../render/settings-scene.h"
static watcher_settings::Runtime desk;
static watcher_settings::Runtime::Emission desk_emission;
#endif
#ifdef WATCHER_TOUCH_SWIPE
#include "touch.h"
#include "paging-controls.h"
#endif
#ifdef WATCHER_TOUCH_DIAGNOSTICS
#include "touch-diagnostic.h"
static bool touch_eligible=false;
static unsigned touch_blocked=0;
#endif
#ifdef WATCHER_TOUCH_USB_DIAGNOSTICS
#include "touch-usb.h"
#ifdef WATCHER_TOUCH_RANGE_DIAGNOSTICS
#include "touch-range-usb.h"
#endif
#endif
#ifdef WATCHER_TOUCH_TIMING_DIAGNOSTICS
#include "touch-timing.h"
static watcher_touch::Timing touch_timing;
#endif
#ifdef WATCHER_LAN
#include "network.h"
#endif
#ifdef WATCHER_GHOST_PAGES
#include "ghost-wire.h"
#endif
#include "motor-off.h"
#ifdef WATCHER_AUTO_ROTATE
#include "imu.h"
#endif
#ifdef WATCHER_COMPLETION_CHIME
#include "completion-chime.h"
#ifdef WATCHER_AUDIO_PATH_PROBE
#include "audio-path-probe.h"
#elif defined(WATCHER_AUDIO_INIT_PROBE)
#include "audio-init-probe.h"
#elif defined(WATCHER_AUDIO_CHIME_PROBE)
#include "audio-chime-probe.h"
#elif defined(WATCHER_FACTORY_FEEDBACK)
#include "factory-feedback-service.h"
#else
#error "The legacy completion backend is retired"
#endif
#endif
#ifdef WATCHER_SUPERVISED_MOTOR_TEST
#include "supervised-pulse.h"
#endif
#include "../../render/ghost.h"
#if defined(WATCHER_RETAINED_PAGING) && !defined(WATCHER_BOUNDED_RENDER)
#error "Retained paging requires the reviewed display-authority integration"
#endif
#if defined(WATCHER_GHOST_FAMILY) && !defined(WATCHER_RETAINED_PAGING)
#error "Ghost family requires the retained paging renderer"
#endif
#if defined(WATCHER_AFTERLIFE) && (!defined(WATCHER_GHOST_FAMILY) || !defined(WATCHER_RETAINED_PAGING))
#error "Afterlife requires ghost family and retained paging"
#endif
#if defined(WATCHER_MOTION_METRICS) && (!defined(WATCHER_RETAINED_PAGING) || !defined(WATCHER_TOUCH_TIMING_DIAGNOSTICS))
#error "Motion metrics requires retained paging and timing diagnostics"
#endif
#ifdef WATCHER_RETAINED_PAGING
#include "../../render/motion-pager.h"
#include "../../render/retained-display.h"
#endif
#ifdef WATCHER_BOUNDED_RENDER
#if !defined(WATCHER_GHOST_PAGES) || !defined(WATCHER_TOUCH_SWIPE) || !defined(WATCHER_LAN) || defined(WATCHER_SUPERVISED_MOTOR_TEST) || defined(WATCHER_FACTORY_ACCEPTANCE) || defined(WATCHER_AUDIO_INIT_PROBE) || defined(WATCHER_AUDIO_CHIME_PROBE)
#error "Bounded rendering currently supports only the normal ghost/touch/LAN profile; special probes retain the legacy renderer"
#endif
#include "../../render/scene-job.h"
#include "../../render/display-transfer.h"
#include "../../render/render-authority.h"
#endif
#ifdef WATCHER_GHOST_PAGES
#include "../../render/focus-transition.h"
#include "../../render/carousel.h"
#endif

namespace {
constexpr int i2c_port=1;
constexpr uint8_t pmic=0x6e,ioe=0x4f;
// Both controller drivers default to 100 kHz; I2C_CFG=0 also selects 100 kHz.
constexpr uint32_t i2c_hz=100000;
constexpr uint16_t background=0x0842;
static bool peripherals_ready=false;
static unsigned board_stage=0;
static uint32_t boot_id=0,last_alert=0,button_id=0,button_kind=0,button_frame=0,displayed_seq=0;
static bool ever_linked=false;
static watcher_wire::Frame frame{};
#ifdef WATCHER_GHOST_PAGES
static watcher_ghost::Page ghost_page;
static watcher_render::FocusTransition focus_transition;
#ifdef WATCHER_RETAINED_PAGING
static watcher_render::MotionPager carousel;
#else
static watcher_render::Carousel carousel;
#endif
static watcher_ghost::FocusContext focus_context;
static bool displayed_preview=false;
static uint32_t paging_event=0;
static int paging_direction=0;
#endif
static int64_t last_frame_ms=-100000;
static unsigned brightness=160;
#ifdef WATCHER_LAN
static watcher_lan::Network network;
static watcher_lan::ProvisionIngress provisioning;
static bool network_started=false,lan_paired=false,exit_requested=false;
enum class LinkSource { none,usb,lan };
static LinkSource link_source=LinkSource::none;
static uint32_t link_generation=0;
#endif
#ifdef WATCHER_AUTO_ROTATE
static watcher_orientation::Imu imu;
#endif
#if defined(WATCHER_AUTO_ROTATE) || defined(WATCHER_TOUCH_SWIPE)
static unsigned rotation=0;
#endif
#ifdef WATCHER_TOUCH_SWIPE
static watcher_touch::Sampler touch_sampler;
static watcher_touch::Swipe swipe;
static uint32_t displayed_selected=0,displayed_count=0,displayed_rotation=0;
static bool touch_buttons=false;
#endif
#ifdef WATCHER_SUPERVISED_MOTOR_TEST
static watcher_probe::Gesture probe_gesture;
static watcher_probe::Pulse probe_pulse;
static bool probe_allowed=false,probe_done=false,probe_ok=false;
static uint8_t probe_hw=0,probe_sw=0,probe_ioe=0,probe_button=0;
static bool probe_checks(){
 return !lgfx::i2c::readRegister(i2c_port,pmic,2,&probe_hw,1,i2c_hz).has_error()
  && !lgfx::i2c::readRegister(i2c_port,pmic,3,&probe_sw,1,i2c_hz).has_error()
  && !lgfx::i2c::readRegister(i2c_port,pmic,0x4a,&probe_button,1,i2c_hz).has_error()
  && !lgfx::i2c::readRegister(i2c_port,ioe,2,&probe_ioe,1,i2c_hz).has_error()
  && probe_hw==5 && probe_sw==6 && !(probe_button&1) && probe_ioe!=0xff;
}
#endif
static int64_t ms(){return esp_timer_get_time()/1000;}
#ifdef WATCHER_BOUNDED_RENDER
static unsigned bounded_rotation=0;
static bool bounded_fault=false,bounded_brightness=false;
// RenderAuthority requires an explicit pre-loss hook even with no auxiliary control owner.
static void bounded_before_loss(void*) noexcept {}
static watcher_render::RenderAuthority bounded_authority(bounded_before_loss,nullptr);
static watcher_render::RenderIdentity bounded_identity(int64_t now){
 const bool linked=frame.seq&&now>=last_frame_ms&&now-last_frame_ms<=1500;
 const bool epochs=focus_context.boot==boot_id;
 return {boot_id,static_cast<unsigned>(link_source),link_generation,frame.selected,frame.count,bounded_rotation,epochs?focus_context.roster:0,epochs?focus_context.selection:0,linked
#ifdef WATCHER_AFTERLIFE
 ,desk.generation()
#endif
 };
}
static void bounded_observe(int64_t now){bounded_authority.observe(bounded_identity(now),frame.seq);displayed_seq=bounded_authority.shown();}
static void bounded_forget(int64_t now){auto retired=bounded_identity(now);retired.linked=false;bounded_authority.observe(retired,0);bounded_authority.invalidate();displayed_seq=bounded_authority.shown();}
#endif
static bool reg_write(uint8_t address,uint8_t reg,uint8_t value){return !lgfx::i2c::writeRegister8(i2c_port,address,reg,value,0,i2c_hz).has_error();}
static bool reg_mask(uint8_t address,uint8_t reg,uint8_t clear,uint8_t set){
 uint8_t value=0;if(lgfx::i2c::readRegister(i2c_port,address,reg,&value,1,i2c_hz).has_error())return false;
 return reg_write(address,reg,(value&~clear)|set);
}
// Default application has no motor activation. The separately built experimental
// image permits one physically armed pulse only; it does not advertise haptic capability.
static bool motor_disable(){return watcher_motor::disable(
 [](uint8_t reg,uint8_t& value){return !lgfx::i2c::readRegister(i2c_port,ioe,reg,&value,1,i2c_hz).has_error();},
 [](uint8_t reg,uint8_t value){return reg_write(ioe,reg,value);});}
static void motor_off(void*){if(peripherals_ready)motor_disable();}
#ifdef WATCHER_COMPLETION_CHIME
#ifdef WATCHER_AUDIO_INIT_PROBE
#ifdef WATCHER_AUDIO_PATH_PROBE
static bool reg_read(uint8_t address,uint8_t reg,uint8_t& value){return !lgfx::i2c::readRegister(i2c_port,address,reg,&value,1,i2c_hz).has_error();}
static watcher_audio::PathProbe audio(reg_mask,reg_write,reg_read,ms);
static uint32_t audio_probe_boot=0;
#else
static watcher_audio::InitProbe audio(reg_mask);
#endif
static watcher_chime::Gate audio_init_gate('A');
static bool audio_probe_requested=false;
#elif defined(WATCHER_AUDIO_CHIME_PROBE)
static watcher_audio::ChimeProbe audio(reg_mask,reg_write,ms);
static watcher_chime::Gate audio_chime_gate('B');
static bool audio_chime_requested=false;
#elif defined(WATCHER_FACTORY_FEEDBACK)
static bool reg_read(uint8_t address,uint8_t reg,uint8_t& value){return !lgfx::i2c::readRegister(i2c_port,address,reg,&value,1,i2c_hz).has_error();}
static bool reg_burst(uint8_t address,uint8_t reg,const uint8_t* values,size_t length){
 if(length!=2||!values)return false;
 const uint8_t bytes[]={reg,values[0],values[1]};
 return !lgfx::i2c::transactionWrite(i2c_port,address,bytes,sizeof bytes,i2c_hz).has_error();
}
static watcher_feedback::Service audio({reg_read,reg_write,reg_burst});
static uint32_t factory_event_boot=0;
#ifdef WATCHER_FACTORY_ACCEPTANCE
static watcher_chime::Gate chime_gate('B');
static bool factory_requested=false;
#else
static watcher_chime::Gate chime_gate;
#endif
#endif
#endif
#ifdef WATCHER_AFTERLIFE
static void desk_context(int64_t now){
 desk.context(boot_id,frame.seq,now,link_source!=LinkSource::none&&frame.seq&&now>=last_frame_ms&&now-last_frame_ms<=1500);
 const unsigned desired=desk.sync().values().brightness;
 if(brightness!=desired){brightness=desired;bounded_brightness=true;}
 audio.outputs(desk.outputs());
}
#endif
static void shutdown(){
#ifdef WATCHER_LAN
 network.stop(); // Request only; chooser path separately waits for acknowledgement.
#endif
#ifdef WATCHER_FACTORY_FEEDBACK
 // Do not race the owner with a second IOE writer if cleanup has not acknowledged.
 if(peripherals_ready&&!audio.deinit())return;
#elif defined(WATCHER_COMPLETION_CHIME)
 if(peripherals_ready)audio.deinit();
#endif
 motor_off(nullptr);
}
static void output(const char* line,size_t length){
#ifdef WATCHER_LAN
 if(link_source!=LinkSource::lan)usb_serial_jtag_write_bytes(line,length,0);
 const auto generation=network.connection();
 if(generation&&(link_source==LinkSource::none||(link_source==LinkSource::lan&&generation==link_generation)))network.send(generation,line,length);
#else
 usb_serial_jtag_write_bytes(line,length,0);
#endif
}
#ifdef WATCHER_TOUCH_USB_DIAGNOSTICS
static watcher_touch::UsbDiagnosticGate touch_usb_gate;
#ifdef WATCHER_TOUCH_RANGE_DIAGNOSTICS
static watcher_touch::RangeWindow touch_range;
static watcher_touch::Context touch_context(int64_t now);
#endif
static bool touch_usb_allowed(int64_t now){
 if(!frame.seq||!displayed_seq||now<last_frame_ms||now-last_frame_ms>1500)return false;
#ifdef WATCHER_LAN
 if(link_source!=LinkSource::usb||exit_requested)return false;
#endif
 return true;
}
static void send_touch_diagnostic(int64_t now){
 const bool allowed=touch_usb_allowed(now);
#ifdef WATCHER_TOUCH_RANGE_DIAGNOSTICS
 touch_range.guard(touch_context(now),now);
#endif
 const auto id=touch_usb_gate.take(now,boot_id,allowed,allowed&&usb_serial_jtag_wait_tx_done(0)==ESP_OK);
 if(!id)return;
 char line[192];size_t length=0;
#ifdef WATCHER_TOUCH_TIMING_DIAGNOSTICS
 if(touch_usb_gate.version()==4){
  uint16_t values[192];char burst[768];touch_timing.snapshot_full(values,touch_sampler,swipe,touch_eligible,now);
  const auto size=watcher_touch::timing_full_lines(burst,boot_id,id,frame.seq,displayed_seq,values);
  if(size)usb_serial_jtag_write_bytes(burst,size,0); // One atomic snapshot/consumed attempt; partial receipt is unavailable.
  return;
 }
 if(touch_usb_gate.version()==3){uint16_t values[48];touch_timing.snapshot(values,touch_sampler,swipe,touch_eligible);length=watcher_touch::timing_line(line,boot_id,id,frame.seq,displayed_seq,values);}else
#endif
#ifdef WATCHER_TOUCH_RANGE_DIAGNOSTICS
 if(touch_usb_gate.version()==2){
  if(touch_usb_gate.mode()==1&&touch_sampler.ready())touch_range.arm(id,touch_context(now),now);
  length=watcher_touch::usb_range_line(line,boot_id,id,frame.seq,displayed_seq,touch_sampler,swipe,touch_eligible,touch_blocked,touch_range.snapshot());
 }else
#endif
 length=watcher_touch::usb_diagnostic_line(line,boot_id,id,frame.seq,displayed_seq,touch_sampler,swipe,touch_eligible,touch_blocked);
 if(length)usb_serial_jtag_write_bytes(line,length,0); // One consumed attempt, no retry or LAN output.
}
#endif
static void send_status(){
 unsigned capabilities=0;
#ifdef WATCHER_LAN
 if(network_started)capabilities|=16;
 if(lan_paired)capabilities|=32;
 if(network.fault())capabilities|=64;
 if(network.has_ip())capabilities|=128;
#endif
#ifdef WATCHER_GHOST_PAGES
 capabilities|=8;
#endif
#ifdef WATCHER_COMPLETION_CHIME
 if(audio.ready())capabilities|=4;
#if defined(WATCHER_HAPTICS) && !defined(WATCHER_FACTORY_ACCEPTANCE)
 if(audio.ready())capabilities|=1;
#endif
#if defined(WATCHER_AUDIO_INIT_PROBE) || defined(WATCHER_AUDIO_CHIME_PROBE) || defined(WATCHER_FACTORY_FEEDBACK)
 if(audio.fault())capabilities|=2;
#endif
#endif
 char line[160];const int n=snprintf(line,sizeof(line),"D2 %lu %lu %lu %lu %lu %lu %lu %u ",(unsigned long)boot_id,(unsigned long)frame.seq,(unsigned long)displayed_seq,(unsigned long)last_alert,(unsigned long)button_id,(unsigned long)button_kind,(unsigned long)button_frame,capabilities);
 if(n<=0 || static_cast<size_t>(n)>=sizeof(line)-16)return;
 const int tail=snprintf(line+n,sizeof(line)-n,"%lu\n",(unsigned long)watcher_wire::crc(line,n));
#ifdef WATCHER_AFTERLIFE
 desk_context(ms());
 const auto source=link_source;const auto generation=link_generation;
 desk.status_batch(line,n+tail,desk_emission,[&](const char* bytes,size_t size){
  return watcher_settings::status_route(source,generation,network.connection(),bytes,size,
   [](const char* data,size_t length){
    // Pinned IDF driver enqueues one byte-buffer item atomically: 0 or size.
    return usb_serial_jtag_write_bytes(data,length,0)==static_cast<int>(length);
   },[](uint32_t active,const char* data,size_t length){return network.send_batch(active,data,length);});
 });
#else
 output(line,n+tail);
#endif
#ifdef WATCHER_GHOST_FAMILY
 // Separate display-format advertisement: all eight legacy D2 capability bits
 // are allocated. Old hosts ignore G0 and continue sending ordinary G1 rows.
 const int family=snprintf(line,sizeof(line),"G0 %lu %lu 2 6 ",(unsigned long)boot_id,(unsigned long)frame.seq);
 if(family>0&&static_cast<size_t>(family)<sizeof(line)-16){const int end=snprintf(line+family,sizeof(line)-family,"%lu\n",(unsigned long)watcher_wire::crc(line,family));output(line,family+end);}
#endif
#ifdef WATCHER_FACTORY_FEEDBACK
 if(factory_event_boot==boot_id){
  const auto result=audio.status();
  const int p=snprintf(line,sizeof(line),"F1 %lu %lu %lu %lu %lu %lu %u ",(unsigned long)boot_id,(unsigned long)frame.seq,(unsigned long)result.errors,(unsigned long)result.sounds,(unsigned long)result.pulses,(unsigned long)result.dma_blocks,result.closed?1:0);
  if(p>0&&static_cast<size_t>(p)<sizeof(line)-16){const int end=snprintf(line+p,sizeof(line)-p,"%lu\n",(unsigned long)watcher_wire::crc(line,p));output(line,p+end);}
 }
#endif
#ifdef WATCHER_AUDIO_PATH_PROBE
 if(audio_probe_boot && audio_probe_boot==boot_id){
  char values[33];for(unsigned i=0;i<16;i++)snprintf(values+2*i,3,"%02x",audio.values()[i]);
  const int p=snprintf(line,sizeof(line),"E1 %lu %lu %u %u %u %lu %u %s ",(unsigned long)boot_id,(unsigned long)frame.seq,audio.stage(),audio.valid(),audio.mismatch(),(unsigned long)audio.blocks(),audio.errors(),values);
  if(p>0 && static_cast<size_t>(p)<sizeof(line)-16){
   const int end=snprintf(line+p,sizeof(line)-p,"%lu\n",(unsigned long)watcher_wire::crc(line,p));
   output(line,p+end);
  }
 }
#endif
#ifdef WATCHER_SUPERVISED_MOTOR_TEST
 const int p=snprintf(line,sizeof(line),"P0 %lu %u %u %u %u %u %u %u %u\n",(unsigned long)boot_id,probe_hw,probe_sw,probe_ioe,probe_button,probe_allowed,static_cast<unsigned>(probe_gesture.phase),probe_done,probe_ok);
 if(p>0&&static_cast<size_t>(p)<sizeof(line))usb_serial_jtag_write_bytes(line,p,0);
#endif
}
struct StopWatchPanel : lgfx::Panel_CO5300 {
 StopWatchPanel(){_cfg.memory_width=_cfg.memory_height=480;}
 const uint8_t* getInitCommands(uint8_t listno) const override {
  static constexpr uint8_t commands[]={0x11,0+CMD_INIT_DELAY,150,0xc4,1,0x80,0x35,1,0x80,0x44,2,0x01,0xd2,0x53,1,0x20,0x20,0,0x36,1,0,0x51,1,0,0x29,0,0xff,0xff};
  return listno==0?commands:nullptr;
 }
};
static lgfx::Bus_SPI bus;
static StopWatchPanel panel;
static lgfx::LGFX_Device display;
static lgfx::LGFX_Sprite canvas(&display);
#ifdef WATCHER_BOUNDED_RENDER
struct BoundedClock {int64_t operator()()const{return esp_timer_get_time();}};
static BoundedClock bounded_clock;
#ifdef WATCHER_RETAINED_PAGING
static watcher_render::RetainedDisplay<lgfx::LGFX_Device,lgfx::Panel_AMOLED_Framebuffer,lgfx::Bus_SPI,BoundedClock> bounded_display(display,bus,bounded_clock);
static watcher_render::SpriteRows motion_sprites;
static watcher_render::RetainedFrame motion_frame;
#else
static watcher_render::DisplayTransfer<lgfx::LGFX_Device,lgfx::LGFX_Sprite,lgfx::Panel_AMOLED_Framebuffer,lgfx::Bus_SPI,BoundedClock> bounded_display(display,canvas,bus,bounded_clock,16);
static watcher_render::SceneJob bounded_scene;
static watcher_render::TransferJob bounded_transfer;
static bool bounded_transferring=false;
#endif
static watcher_render::SceneProgram bounded_program;
static bool bounded_boot_fence(){
 for(unsigned turn=0;turn<8&&bounded_display.pending()&&!bounded_display.faulted();++turn){
  if(bounded_display.finish_band())break;
  vTaskDelay(1); // Boot only;50ms busy timeout, never a render service recursion.
 }
 return bounded_display.ready();
}
#endif
static void clear_for_restart(){
#ifdef WATCHER_BOUNDED_RENDER
 bounded_forget(ms()); // Never start another framebuffer flush during shutdown.
#else
 display.fillScreen(background);
#endif
}
static bool init_board(){
 board_stage=10;
 if(lgfx::i2c::init(i2c_port,47,48).has_error())return false;
 board_stage=20;
 uint16_t identity=0;
 if(lgfx::i2c::readRegister(i2c_port,pmic,0,reinterpret_cast<uint8_t*>(&identity),2,i2c_hz).has_error() || identity!=0x2050)return false;
 board_stage=30;
 if(lgfx::i2c::readRegister(i2c_port,ioe,0,reinterpret_cast<uint8_t*>(&identity),2,i2c_hz).has_error())return false;
 peripherals_ready=true;
 board_stage=40;
 if(!motor_disable())return false;
 board_stage=45;
 // No PWM frequency/enable writes; GPIO9 low was read back before PWM was disabled.
 if(!reg_write(ioe,0x23,0))return false;
 // Public M5Stack power/reset sequence, not conference application code.
 board_stage=50;
 if(!reg_write(pmic,0x09,0) || !reg_write(pmic,0x0a,0) || !reg_mask(pmic,0x06,0,0x17))return false;
 board_stage=60;
 gpio_set_direction(GPIO_NUM_39,GPIO_MODE_OUTPUT);gpio_set_level(GPIO_NUM_39,1);
 if(!reg_mask(ioe,0x13,0x9d,0) || !reg_mask(ioe,0x03,0,0x9d) || !reg_mask(ioe,0x05,0x04,0x99))return false;
 board_stage=61;
 // Preserve the vendor's minimum reset/settle times. At our 100 Hz tick,
 // pdMS_TO_TICKS(8) and (2) are zero; lgfx::delay yields then waits out the remainder.
 lgfx::delay(10);if(!reg_mask(ioe,0x05,0x18,0))return false;
 board_stage=62;
 lgfx::delay(8);if(!reg_mask(ioe,0x05,0,0x18))return false;
 lgfx::delay(2);
 board_stage=63;
 if(!reg_mask(ioe,0x14,2,0))return false;
 board_stage=64;
 if(!reg_mask(ioe,0x04,0,2))return false;
 board_stage=65;
 if(!reg_mask(ioe,0x06,2,0))return false; // audio PA off; failure remains fatal
 auto config=bus.config();config.spi_host=SPI2_HOST;config.spi_mode=0;config.spi_3wire=true;
 config.pin_mosi=-1;config.pin_miso=-1;config.pin_io0=41;config.pin_io1=42;config.pin_io2=46;config.pin_io3=45;config.pin_sclk=40;
 config.freq_write=80000000;config.freq_read=1000000;bus.config(config);panel.bus(&bus);
 auto pc=panel.config();pc.pin_cs=39;pc.pin_rst=-1;pc.pin_busy=-1;pc.panel_width=468;pc.panel_height=468;pc.memory_width=480;pc.memory_height=480;pc.offset_x=6;pc.offset_y=0;pc.offset_rotation=0;pc.readable=false;pc.invert=false;pc.bus_shared=false;panel.config(pc);
 gpio_set_direction(GPIO_NUM_38,GPIO_MODE_INPUT);gpio_set_pull_mode(GPIO_NUM_38,GPIO_PULLUP_ONLY);
 board_stage=70;
 // Reset already performed through the IOE. Do not clear through the raw panel:
 // StopWatch uses the vendor's aligned, DMA-backed framebuffer display path.
 display.setPanel(&panel);if(!display.init_without_reset(false))return false;
 board_stage=75;
 if(!panel.initPanelFb())return false;
 auto* framebuffer=panel.getPanelFb();if(!framebuffer)return false;
 framebuffer->setBus(&bus);framebuffer->setAutoDisplay(true);
 display.setPanel(framebuffer);
 display.setRotation(0);
#ifndef WATCHER_BOUNDED_RENDER
 display.fillScreen(background);
#endif
 board_stage=80;
 #ifdef WATCHER_RETAINED_PAGING
 if(!motion_sprites.initialize())return false;
#else
 canvas.setColorDepth(16);canvas.setPsram(true);if(!canvas.createSprite(466,466))return false;
#endif
#ifdef WATCHER_BOUNDED_RENDER
 board_stage=81;
 if(!bounded_display.initialize(*framebuffer,background)||!bounded_boot_fence())return false;
#endif
 board_stage=90;
 gpio_set_direction(GPIO_NUM_1,GPIO_MODE_INPUT);gpio_set_pull_mode(GPIO_NUM_1,GPIO_FLOATING);
 gpio_set_direction(GPIO_NUM_2,GPIO_MODE_INPUT);gpio_set_pull_mode(GPIO_NUM_2,GPIO_FLOATING);
 board_stage=100;
#ifdef WATCHER_BOUNDED_RENDER
 board_stage=91;
 if(!bounded_display.apply_brightness(brightness)||!bounded_boot_fence())return false;
#else
 display.setBrightness(brightness); // Only illuminate after framebuffer/canvas setup succeeds.
#endif
 return true;
}
static void expire_link(int64_t now){
 if(!frame.seq || now-last_frame_ms<=1500)return;
#ifdef WATCHER_BOUNDED_RENDER
 bounded_forget(now);
#endif
#ifdef WATCHER_COMPLETION_CHIME
#ifdef WATCHER_AUDIO_INIT_PROBE
 audio.deinit();
#else
 audio.stop();
#endif
#endif
 uint32_t fresh=0;do{fresh=esp_random();}while(!fresh || fresh==boot_id);
 boot_id=fresh;frame.seq=0;last_alert=0;button_id=0;button_kind=0;button_frame=0;displayed_seq=0;
#ifdef WATCHER_TOUCH_TIMING_DIAGNOSTICS
 touch_timing.retire();
#endif
#ifdef WATCHER_GHOST_PAGES
 paging_event=0;paging_direction=0;focus_context={};focus_transition.reset();carousel.reset();
#endif
#ifdef WATCHER_LAN
 if(link_source==LinkSource::lan)network.drop(link_generation);
 link_source=LinkSource::none;link_generation=0;provisioning.clear();
#endif
#ifdef WATCHER_BOUNDED_RENDER
 bounded_observe(now);
#endif
 send_status();
}
static void accept(const char* line,size_t length){
 expire_link(ms()); // Reject buffered pre-outage commands before processing any frame.
 if(length==2 && !memcmp(line,"Q1",2)){send_status();return;}
#ifdef WATCHER_AFTERLIFE
 if(length>=3&&!memcmp(line,"S1 ",3)){
  desk_context(ms());
  if(desk.receive(line,length)){desk_context(ms());bounded_observe(ms());}
  return; // No liveness, ACK, notification, or transport authority from S1.
 }
#endif
#ifdef WATCHER_TOUCH_USB_DIAGNOSTICS
 if(length>=4&&(!memcmp(line,"TQ1 ",4)
#ifdef WATCHER_TOUCH_TIMING_DIAGNOSTICS
  ||!memcmp(line,"TQ3 ",4)||!memcmp(line,"TQ4 ",4)
#endif
#ifdef WATCHER_TOUCH_RANGE_DIAGNOSTICS
  ||!memcmp(line,"TQ2 ",4)
#endif
 )){
  const auto now=ms();touch_usb_gate.request(line,length,now,boot_id,touch_usb_allowed(now));
  return; // Diagnostic admission only: never renew liveness, select a link, ACK, page, record audio, or touch I2C.
 }
#endif
#ifdef WATCHER_GHOST_PAGES
 if(length>=3&&!memcmp(line,"K1 ",3)){
  focus_context.accept(line,length,frame);
#ifdef WATCHER_BOUNDED_RENDER
  bounded_observe(ms()); // Observe each identity seam, including ABA in one USB batch.
#endif
  return; // Display identity only, never liveness or selection authority.
 }
 if(length>=3&&(!memcmp(line,"G1 ",3)
#ifdef WATCHER_GHOST_FAMILY
  ||!memcmp(line,"G2 ",3)
#endif
 )){
  watcher_ghost::Row row;
  if(watcher_ghost::parse(line,length,row))ghost_page.accept(row,frame,ms());
  return; // Never update link freshness, sequence, buttons, sound or motor from metadata.
 }
#endif
#ifdef WATCHER_COMPLETION_CHIME
#ifdef WATCHER_AUDIO_INIT_PROBE
 if(length>=3 && !memcmp(line,"A1 ",3)){
  const auto now=ms();
  if(!audio_probe_requested && button_kind!=4 && audio_init_gate.take(line,length,frame,now,frame.seq && now-last_frame_ms<=1500)){
   audio_probe_requested=true; // Explicit one-shot initialization, never playback.
#ifdef WATCHER_AUDIO_PATH_PROBE
   audio_probe_boot=boot_id;
#endif
   audio.init(now);
   send_status();
  }
  return;
 }
#elif defined(WATCHER_AUDIO_CHIME_PROBE)
 if(length>=3 && !memcmp(line,"B1 ",3)){
  const auto now=ms();
  if(!audio_chime_requested && button_kind!=4 && audio_chime_gate.take(line,length,frame,now,frame.seq && now-last_frame_ms<=1500)){
   audio_chime_requested=true;
   audio.start();
   send_status();
  }
  return;
 }
#elif defined(WATCHER_FACTORY_FEEDBACK)
#ifdef WATCHER_FACTORY_ACCEPTANCE
 constexpr const char* prefix="B1 ";
#else
 constexpr const char* prefix="C1 ";
#endif
 if(length>=3&&!memcmp(line,prefix,3)){
  const auto now=ms();
#ifdef WATCHER_FACTORY_ACCEPTANCE
  if(factory_requested)return;
#endif
  if(chime_gate.take(line,length,frame,now,frame.seq&&now-last_frame_ms<=1500)&&button_kind!=4){
#ifdef WATCHER_FACTORY_ACCEPTANCE
   factory_requested=true; // Consume before dispatch, including ambiguous/failed dispatch.
#endif
   factory_event_boot=boot_id;
#ifdef WATCHER_AFTERLIFE
   desk_context(now);audio.start(now,desk.outputs());
#else
   audio.start(now);
#endif
  }
  return;
 }
#endif
#endif
 watcher_wire::Frame next{};
 if(!watcher_wire::parse(line,length,next) || !watcher_wire::fresh(next,boot_id,frame.seq))return;
 frame=next;last_frame_ms=ms();ever_linked=true;
#ifdef WATCHER_AFTERLIFE
 desk_context(last_frame_ms); // Before the next S1 in the same ordered read.
#endif
#ifdef WATCHER_BOUNDED_RENDER
 bounded_observe(last_frame_ms);
#endif
#ifdef WATCHER_TOUCH_TIMING_DIAGNOSTICS
 touch_timing.ack(frame.event_ack,frame.selected,last_frame_ms);
#endif
 if(button_kind && frame.event_ack>=button_id)button_kind=0;
#ifdef WATCHER_FACTORY_FEEDBACK
 audio.context(last_frame_ms+1500,!(frame.flags&1)&&button_kind!=4);
#ifdef WATCHER_AFTERLIFE
 audio.alert(frame,true,desk.outputs()); // Zero mask still consumes the event.
#else
 audio.alert(frame); // Gate consumes muted/ineligible events; acceptance mode cannot dispatch alerts.
#endif
#endif
 // Acceptance is not vibration proof. Haptic capability is zero unless explicitly enabled in a normal build.
 if(frame.alert>last_alert)last_alert=frame.alert;
 send_status();
}
#ifdef WATCHER_LAN
static void dispatch(watcher_lan::Source source,uint32_t generation,const char* line,size_t length,int64_t received){
 const auto at=ms();expire_link(at);
 if(source==watcher_lan::Source::lan&&(generation!=network.connection()||!generation||at<received||at-received>250)){network.drop(generation);return;}
 const uint32_t provisioning_boot=boot_id;
 const auto provisioned=provisioning.dispatch(source,line,length,boot_id,at,[&](const watcher_lan::Credentials& credentials,uint32_t transaction){
  if(!network.submit(credentials))return false;
  lan_paired=true;
  char reply[96];const int n=snprintf(reply,sizeof reply,"P2 %lu %lu 1 ",(unsigned long)provisioning_boot,(unsigned long)transaction);
  const int tail=snprintf(reply+n,sizeof reply-n,"%lu\n",(unsigned long)watcher_wire::crc(reply,n));
  usb_serial_jtag_write_bytes(reply,n+tail,0); // RAM acceptance, not connection success.
  if(link_source==LinkSource::lan){last_frame_ms=ms()-1501;expire_link(ms());}
  return true;
 });
 if(provisioned!=watcher_lan::IngressResult::ordinary)return;
 const auto wanted=source==watcher_lan::Source::usb?LinkSource::usb:LinkSource::lan;
 if(link_source!=LinkSource::none&&(link_source!=wanted||(wanted==LinkSource::lan&&generation!=link_generation)))return;
 watcher_wire::Frame next{};
 if(watcher_wire::parse(line,length,next)&&watcher_wire::fresh(next,boot_id,frame.seq)){
  link_source=wanted;link_generation=wanted==LinkSource::lan?generation:0;
 }
 accept(line,length);
}
#endif
static void event(uint32_t kind,uint32_t shown,uint32_t observed_boot){
 if(observed_boot!=boot_id || button_kind || button_id==UINT32_MAX || !frame.seq || !shown || ms()-last_frame_ms>1500){
#ifdef WATCHER_TOUCH_TIMING_DIAGNOSTICS
  if(kind==1||kind==2)touch_timing.denied();
#endif
  return;
 }
 ++button_id;button_kind=kind;button_frame=shown;
#ifdef WATCHER_TOUCH_TIMING_DIAGNOSTICS
 if(kind==1||kind==2)touch_timing.issue(button_id,ms());
#endif
 send_status();
}
static void buttons(int64_t now){
 static watcher_buttons::Buttons state;
 const unsigned value=(!gpio_get_level(GPIO_NUM_2)?1:0)|(!gpio_get_level(GPIO_NUM_1)?2:0);
 bool block_preview_controls=false;
#if defined(WATCHER_GHOST_PAGES) && defined(WATCHER_TOUCH_SWIPE)
 static watcher_touch::PreviewButtons preview_buttons;
 const bool ambiguous=displayed_preview||swipe.tracking()||paging_event
#ifdef WATCHER_BOUNDED_RENDER
  ||bounded_fault||bounded_authority.controls_blocked(bounded_identity(now))
#endif
 ;
 block_preview_controls=preview_buttons.update(value,now,ambiguous);
 if(value&&ambiguous){swipe.cancel();focus_transition.reset();carousel.reset();}
#endif
 auto input=state.update(value,now,boot_id,displayed_seq);
 using watcher_buttons::Action;
 if(block_preview_controls&&input.action==Action::ack)input.action=Action::none;
#ifdef WATCHER_TOUCH_SWIPE
 touch_buttons=value||input.action!=Action::none;
#endif
#ifdef WATCHER_SUPERVISED_MOTOR_TEST
 if(probe_allowed && probe_gesture.update(value,now)){
  probe_done=true; // Consumed before any potentially ambiguous motor write.
  probe_ok=probe_checks() && probe_pulse.run(
   [](uint8_t reg,uint8_t& val){return !lgfx::i2c::readRegister(i2c_port,ioe,reg,&val,1,i2c_hz).has_error();},
   [](uint8_t reg,uint8_t val){return reg_write(ioe,reg,val);},
   [](){return motor_disable();},[](unsigned duration){vTaskDelay(pdMS_TO_TICKS(duration));});
  send_status();
 }
 // No paging/ACK/mute gestures in this special image. Physical power button is untouched.
 if(input.action!=Action::exit_to_init)return;
#endif
#ifdef WATCHER_AFTERLIFE
 desk_context(now);
 if(value)desk.cancel(); // Physical contact also pauses automatic navigation.
 if(desk.open()){
  if(input.action==Action::previous&&bounded_authority.local_touch_allowed(bounded_identity(now))){desk.back();bounded_observe(now);}
  if(input.action==Action::previous||input.action==Action::next||input.action==Action::ack)input.action=Action::none;
 }
#endif
 switch(input.action){
  case Action::previous:event(1,input.frame,input.boot);break;
  case Action::next:event(2,input.frame,input.boot);break;
  case Action::ack:event(3,input.frame,input.boot);break;
  case Action::mute:
#ifdef WATCHER_COMPLETION_CHIME
   audio.stop(); // Local silence immediately, before the host processes mute.
#endif
#ifdef WATCHER_AFTERLIFE
   // Master mute is global, not a session ACK. Settings publishes no shown seq.
   event(4,desk.open()?frame.seq:input.frame,input.boot);break;
#else
   event(4,input.frame,input.boot);break;
#endif
  case Action::exit_to_init:
#ifdef WATCHER_LAN
   exit_requested=true;break;
#else
#ifdef WATCHER_FACTORY_FEEDBACK
   if(!audio.deinit())break; // Never return to the chooser while owner cleanup is outstanding.
#endif
   shutdown();clear_for_restart();esp_restart();break;
#endif
  case Action::brightness:
#ifdef WATCHER_AFTERLIFE
   desk_context(now);desk.change(4);desk_context(now);bounded_observe(now);
#else
   brightness=brightness==80?160:brightness==160?240:80;
#ifdef WATCHER_BOUNDED_RENDER
   bounded_brightness=true;
#else
   display.setBrightness(brightness);
#endif
#endif
   break;
  default:break;
 }
}
#ifdef WATCHER_TOUCH_SWIPE
static watcher_touch::Context touch_context(int64_t now){
 bool eligible=frame.seq&&now-last_frame_ms<=1500&&!button_kind&&!touch_buttons&&
  displayed_seq&&displayed_selected==frame.selected&&displayed_count==frame.count&&displayed_rotation==rotation;
#ifdef WATCHER_BOUNDED_RENDER
 eligible=eligible&&!bounded_fault&&bounded_authority.touch_allowed(bounded_identity(now));
#endif
#ifdef WATCHER_LAN
 eligible=eligible&&!exit_requested;
 const unsigned source=static_cast<unsigned>(link_source),generation=link_generation;
#else
 const unsigned source=1,generation=0;
#endif
 uint32_t roster=0,selection=0;
#ifdef WATCHER_GHOST_PAGES
 if(focus_context.boot==frame.boot){roster=focus_context.roster;selection=focus_context.selection;}
 // A partial page may retain the exact old raster, not a responsive preview.
 // Stop the gesture rather than letting metadata stalls commit unseen motion.
 if(ghost_page.mode(frame,now)==watcher_ghost::Mode::waiting&&now-last_frame_ms>100)eligible=false;
#endif
 return {eligible,boot_id,displayed_seq,frame.selected,frame.count,rotation,source,generation,roster,selection};
}
static void touch(int64_t now){
 const auto context=touch_context(now);
#ifdef WATCHER_TOUCH_RANGE_DIAGNOSTICS
 touch_range.guard(context,now);
#endif
 swipe.expire(now);
 const bool allowed=swipe.guard(context,now);
#ifdef WATCHER_TOUCH_DIAGNOSTICS
 touch_eligible=allowed;
 touch_blocked=(!frame.seq||now-last_frame_ms>1500?1u:0u)|(button_kind?2u:0u)|(touch_buttons?4u:0u)|
  (!displayed_seq||displayed_selected!=frame.selected||displayed_count!=frame.count||displayed_rotation!=rotation?8u:0u)|(frame.count<=1?64u:0u);
#ifdef WATCHER_LAN
 if(exit_requested)touch_blocked|=32u;
#endif
 if(!allowed&&!touch_blocked)touch_blocked=128u;
#ifdef WATCHER_TOUCH_TIMING_DIAGNOSTICS
 bool metadata_blocked=false;
#ifdef WATCHER_GHOST_PAGES
 metadata_blocked=ghost_page.mode(frame,now)==watcher_ghost::Mode::waiting&&now-last_frame_ms>100;
#endif
 touch_timing.gate(ms(),allowed,touch_blocked,metadata_blocked);
 if(frame.seq)touch_timing.frame_age(ms()-last_frame_ms);
#endif
#endif
#ifdef WATCHER_AFTERLIFE
 const bool local_allowed=!bounded_fault&&!button_kind&&!touch_buttons&&!exit_requested&&rotation==bounded_rotation&&bounded_authority.local_touch_allowed(bounded_identity(now));
 if(!local_allowed){if(desk.activity()&2)desk.cancel();}
 if(!allowed&&!local_allowed)return;
#else
 if(!allowed)return; // No touch bus traffic during button/retired-display authority.
#endif
 watcher_touch::Sample sample;
 if(!touch_sampler.poll(now,sample,
   [](uint8_t reg,uint8_t* data,unsigned size){
#ifdef WATCHER_TOUCH_TIMING_DIAGNOSTICS
    const auto began=ms();
#endif
    const bool ok=!lgfx::i2c::readRegister(i2c_port,0x15,reg,data,size,i2c_hz).has_error();
#ifdef WATCHER_TOUCH_TIMING_DIAGNOSTICS
    if(reg==0x02)touch_timing.read(began,ms());
#endif
#ifdef WATCHER_TOUCH_RANGE_DIAGNOSTICS
    if(reg==0x02){const auto at=ms();touch_range.guard(touch_context(at),at);touch_range.observe(data,size,ok,at);}
#endif
    return ok;
   },
   [](uint8_t reg,uint8_t value){return reg_write(0x15,reg,value);}))return;
#ifdef WATCHER_AFTERLIFE
 desk_context(ms());
 const auto local=desk.touch(sample,local_allowed&&bounded_authority.local_touch_allowed(bounded_identity(ms())),rotation);
 desk_context(ms());bounded_observe(ms()); // Apply local disable/brightness before more work.
 if(local.claimed||desk.open()){swipe.cancel();return;}
 if(!allowed)return;
#endif
 const auto gesture=swipe.update(sample,context,now);
 if(gesture.kind)event(gesture.kind,gesture.shown,gesture.boot);
#ifdef WATCHER_GHOST_PAGES
 if(gesture.kind&&button_kind==gesture.kind&&button_frame==gesture.shown){paging_event=button_id;paging_direction=gesture.kind==2?1:-1;}
#endif
}
#endif
static void text(const char* value,int y,float size,uint16_t color,uint16_t text_background=background){
#ifdef WATCHER_RETAINED_PAGING
 display.setTextSize(size);display.setTextColor(color,text_background);display.drawCenterString(value,233,y,&lgfx::fonts::Font0);
#else
 canvas.setTextSize(size);canvas.setTextColor(color,text_background);canvas.drawCenterString(value,233,y,&lgfx::fonts::Font0);
#endif
}
#ifndef WATCHER_BOUNDED_RENDER
static void render(int64_t now){
 const bool linked=frame.seq && now-last_frame_ms<=1500;
#ifdef WATCHER_GHOST_PAGES
 const auto page_mode=ghost_page.mode(frame,now);
 const bool group=linked&&(page_mode==watcher_ghost::Mode::group||page_mode==watcher_ghost::Mode::carousel);
 unsigned shown_mask=0,shown_count=0;
#ifdef WATCHER_TOUCH_TIMING_DIAGNOSTICS
 touch_timing.metadata(linked&&page_mode==watcher_ghost::Mode::waiting,ms());
#endif
 if(linked&&page_mode==watcher_ghost::Mode::waiting)return; // Bounded hold, retaining the exact rendered ACK identity.
 const auto* focused=group?ghost_page.get(frame.selected,frame):nullptr;
#endif
 const unsigned state=linked?frame.state:(ever_linked?8:9);
 const unsigned art_state=state==9?8:state;
 const bool reduced=frame.flags&8;
 const float t=now/1000.0f,speed=state==2?1.15f:state==1?.64f:.42f;
 const bool animated=linked && state<7 && !reduced;
 const int x=233+(animated?static_cast<int>(sinf(t*speed)*(state==2?21:state>=3?4:12)):0);
 int y=215+(animated?static_cast<int>(sinf(t*speed*1.37f)*(state==2?7:5)):0);
 if(animated && state==6)y=215-static_cast<int>(powf(fmaxf(0,sinf(t*1.3f)),4)*5);
 canvas.fillScreen(background);
#ifdef WATCHER_GHOST_PAGES
 if(focused&&watcher_render::multi_session_layout(frame.count)){
  unsigned indices[4]{},visible=0;
  watcher_render::CarouselPose positions[4]{};
  watcher_render::FocusScene scene;scene.boot=frame.boot;scene.count=frame.count;scene.selected=frame.selected;
#if defined(WATCHER_AUTO_ROTATE) || defined(WATCHER_TOUCH_SWIPE)
  scene.rotation=rotation;
#endif
#ifdef WATCHER_LAN
  scene.source=static_cast<unsigned>(link_source);scene.generation=link_generation;
#endif
  const bool identity_ready=focus_context.matches(frame);
  if(identity_ready){scene.roster=focus_context.roster;scene.selection_epoch=focus_context.selection;}
  bool move_focus=!reduced;
  bool dragging=false;int displacement=0;
#ifdef WATCHER_TOUCH_SWIPE
  dragging=identity_ready&&swipe.tracking();displacement=swipe.displacement();
#endif
  const unsigned release=paging_event?(frame.event_ack>=paging_event?2u:1u):0u;
  if(page_mode==watcher_ghost::Mode::carousel&&identity_ready){
   focus_transition.reset();
   const watcher_render::CarouselScene ring{scene.boot,scene.generation,scene.source,scene.rotation,scene.count,scene.selected,scene.roster,scene.selection_epoch};
   if(!carousel.update(ring,now,move_focus,dragging,displacement,release,paging_direction))return;
   visible=carousel.order(indices);
   if(!visible){carousel.reset();return;}
   for(unsigned n=0;n<visible;n++)positions[n]=carousel.pose(indices[n]);
  }else{
   // Older hosts without the neighbor window retain the previous page layout.
   carousel.reset();
   const unsigned start=frame.selected/4*4;
   visible=(frame.count-start)<4?frame.count-start:4;
   for(unsigned slot=0;slot<visible;slot++){
    const auto* row=ghost_page.get(start+slot,frame);
    if(!row){focus_transition.reset();return;}
    scene.hue[slot]=row->hue;scene.rhythm[slot]=row->rhythm;
   }
   if(!focus_transition.update(scene,now,move_focus,dragging,displacement,release))return;
   unsigned order[4];focus_transition.order(order);
   for(unsigned n=0;n<visible;n++){
    indices[n]=start+order[n];const auto p=focus_transition.pose(order[n]);
    positions[n]={p.x,p.y,p.scale,0,255};
   }
  }
  for(unsigned n=0;n<visible;n++)if(!ghost_page.get(indices[n],frame)){focus_transition.reset();carousel.reset();return;}
  if(release==2)paging_event=0;
  for(unsigned n=0;n<visible;n++){
   const unsigned i=indices[n];
   const auto* row=ghost_page.get(i,frame);
   if(!row){focus_transition.reset();carousel.reset();return;}
   const auto position=positions[n];
   shown_mask|=1u<<i;++shown_count;
   const auto sink=[&](int xx,int yy,int length,uint16_t color){canvas.fillRect(xx,yy,length,1,watcher_render::carousel_color(color,background,position.alpha));};
   if(i==frame.selected){
    const auto pose=watcher_render::personality_pose(static_cast<watcher_render::State>(art_state),reduced,now,focused->rhythm,false);
    watcher_render::draw_personality(static_cast<watcher_render::State>(art_state),pose.blink,position.x+pose.x,position.y+pose.y,position.scale,focused->hue,focused->rhythm,sink);
   }else{
    const auto companion=watcher_render::personality_pose(static_cast<watcher_render::State>(row->state),reduced,now,row->rhythm,true);
    const int xx=position.x+companion.x,yy=position.y+companion.y;
    watcher_render::draw_personality(static_cast<watcher_render::State>(row->state),companion.blink,xx,yy,position.scale,row->hue,row->rhythm,sink);
    if(row->pending){const int badge_x=xx+25*position.scale/66,badge_y=yy-23*position.scale/66;const auto color=watcher_render::carousel_color(0xf672,background,position.alpha);canvas.drawCircle(badge_x,badge_y,6,color);if(row->unseen)canvas.fillCircle(badge_x,badge_y,3,color);}
   }
  }
 }else
#endif
 {
#ifdef WATCHER_GHOST_PAGES
  focus_transition.reset();carousel.reset();
  if(focused){shown_mask=1u<<frame.selected;shown_count=1;}
#endif
  canvas.fillEllipse(233,320,60,9,(uint16_t)0x28e7);
  watcher_render::draw_ghost(static_cast<watcher_render::State>(art_state),animated && now%5700>5520,x,y,[](int xx,int yy,int n,uint16_t color){canvas.fillRect(xx,yy,n,1,color);});
 }
#ifdef WATCHER_SUPERVISED_MOTOR_TEST
 text("Supervised motor test",340,2.0f,0xef7d);
 if(!probe_allowed){text("Test locked",381,2.0f,0xf672);text("Inspect PMIC revision/settings",410,1.3f,0xbdd7);}
 else if(probe_done){text(probe_ok?"Off readback OK":"POWER OFF NOW",381,2.0f,probe_ok?0xef7d:0xf672);text("Hold A 4s: return to init()",410,1.3f,0xbdd7);}
 else if(probe_gesture.phase==watcher_probe::Gesture::Phase::armed || probe_gesture.phase==watcher_probe::Gesture::Phase::pressed){text("Tap A once to test",381,2.0f,0xf672);text("80 ms / 25% / no repeat",410,1.3f,0xbdd7);}
 else {text("Hold A+B for 2 seconds",381,1.6f,0xef7d);text("Release both, then tap A",410,1.3f,0xbdd7);}
 canvas.pushSprite(0,0);displayed_seq=0;return;
#endif
 const char* labels[]={"All quiet","Thinking","Working","Needs you","Child needs help","All done","Failed","Status unknown","Disconnected","Connect to Pi"};
 char line[64];
#ifdef WATCHER_GHOST_PAGES
 if(focused)snprintf(line,sizeof(line),"%s (%lu)",focused->title,(unsigned long)focused->space_count);
 else
#endif
 if(frame.count)snprintf(line,sizeof(line),"Session %lu",(unsigned long)frame.selected+1);else snprintf(line,sizeof(line),"Pi watcher");
 text(line,53,strlen(line)>24?1.6f:2.5f,0xef7d);
 for(unsigned i=0;i<frame.count;i++)canvas.fillCircle(233+(static_cast<int>(i)-(static_cast<int>(frame.count)-1)/2)*12,91,i==frame.selected?4:2,(uint16_t)0xb43a);
 text(labels[state],340,state==4?2.5f:3.0f,state>=3 && state<=6 && state!=5?0xf672:0xef7d);
 const char* detail="Nothing needs you yet";
 if(state==3)detail=(frame.flags&2)?"Acknowledged; still waiting":"Waiting for your input";
 else if(state==4){snprintf(line,sizeof(line),"%lu pending request%s",(unsigned long)frame.pending,frame.pending==1?"":"s");detail=(frame.flags&2)?"Acknowledged; still waiting":line;}
 else if(state==5)detail="Session complete";
 else if(state==6)detail=(frame.flags&2)?"Acknowledged; failure retained":"Check this session";
 else if(state==7)detail="Waiting for fresh status";
 else if(state==8)detail=(frame.flags&16)?"Request retained":"Reconnect to your Mac";
 else if(state==9)detail="Start the USB host bridge";
 if((frame.flags&4) && state<7)detail="Source status incomplete";
#ifdef WATCHER_GHOST_PAGES
 if(focus_transition.previewing()||carousel.previewing())detail=paging_event?"Waiting for selection":"Preview; release to switch";
#endif
 text(detail,381,1.5f,0xbdd7);
 const char* feedback_label="Haptics off";
#if defined(WATCHER_HAPTICS) && !defined(WATCHER_FACTORY_ACCEPTANCE)
 feedback_label=audio.ready()?"Haptics on":"Feedback fault";
#endif
 if(frame.flags&1)feedback_label="Muted";
#ifdef WATCHER_GHOST_PAGES
 if(focused){
  unsigned local_waiting=0;
  for(unsigned i=0;i<frame.count;i++)if(shown_mask&(1u<<i)){const auto* row=ghost_page.get(i,frame);if(i!=frame.selected&&row&&row->pending)local_waiting++;}
  const unsigned hidden_waiting=frame.other>local_waiting?frame.other-local_waiting:0;
  snprintf(line,sizeof(line),"%lu offscreen / %u waiting / %s",(unsigned long)(frame.count-shown_count),hidden_waiting,feedback_label);
 }else
#endif
 if(frame.other)snprintf(line,sizeof(line),"%lu elsewhere / %s",(unsigned long)frame.other,feedback_label);
 else snprintf(line,sizeof(line),"%lu / %lu / %s",(unsigned long)(frame.count?frame.selected+1:0),(unsigned long)frame.count,feedback_label);
 text(line,410,1.4f,0xa514);
#if defined(WATCHER_TOUCH_DIAGNOSTICS) && !defined(WATCHER_TOUCH_TIMING_DIAGNOSTICS)
 bool show_touch=true;
 if(show_touch){char top[48],bottom[48];watcher_touch::diagnostic_lines(top,bottom,touch_sampler,swipe,touch_eligible,touch_blocked);text(top,427,1.0f,0xbdd7);text(bottom,443,1.0f,0xbdd7);}
#endif
 canvas.pushSprite(0,0);
#ifdef WATCHER_TOUCH_TIMING_DIAGNOSTICS
 touch_timing.paint(frame.selected,ms(),linked);
#endif
#ifdef WATCHER_GHOST_PAGES
 displayed_preview=focus_transition.previewing()||carousel.previewing();
 if(!linked||(paging_event&&frame.event_ack>=paging_event))paging_event=0;
#endif
 displayed_seq=linked?frame.seq:0;
#ifdef WATCHER_TOUCH_SWIPE
 displayed_selected=frame.selected;displayed_count=frame.count;displayed_rotation=rotation;
#endif
}
#else
#include "bounded-scene.inc"
#ifdef WATCHER_RETAINED_PAGING
#include "motion-loop.inc"
#else
#include "bounded-loop.inc"
#endif
#endif
} // namespace
extern "C" void app_main(){
 usb_serial_jtag_driver_config_t usb{.tx_buffer_size=2048,.rx_buffer_size=2048};
 ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb));
 do{boot_id=esp_random();}while(!boot_id);
 if(!init_board()){
  motor_off(nullptr);
  char error[80];const int n=snprintf(error,sizeof(error),"WATCHER_BOARD_INIT_FAILED stage=%u\n",board_stage);
  usb_serial_jtag_write_bytes(error,n,pdMS_TO_TICKS(100));
  vTaskDelay(pdMS_TO_TICKS(2000));esp_restart();
 }
#ifdef WATCHER_AUTO_ROTATE
 imu.init(); // Failure leaves the display usable at its last orientation; no retry storms.
#endif
#if defined(WATCHER_COMPLETION_CHIME) && !defined(WATCHER_AUDIO_INIT_PROBE) && !defined(WATCHER_AUDIO_CHIME_PROBE)
 audio.init(); // Starts an idle owner task only: no rail, codec, clocks, PA or motor at boot.
#endif
#ifdef WATCHER_SUPERVISED_MOTOR_TEST
 probe_allowed=probe_checks();
#endif
#ifdef WATCHER_LAN
 network_started=network.start(); // Idle task only; radio waits for USB provisioning.
#endif
 ESP_ERROR_CHECK(esp_register_shutdown_handler(shutdown));
 char line[watcher_wire::max_line];size_t used=0;bool discard=false;int64_t rendered=-1000,reported=-1000;
 while(true){
#ifdef WATCHER_TOUCH_TIMING_DIAGNOSTICS
  int64_t timing_begin=ms();touch_timing.loop(timing_begin);
#endif
  char bytes[256];const int length=usb_serial_jtag_read_bytes(bytes,sizeof(bytes),0);
  for(int i=0;i<length;i++){
   const char c=bytes[i];if(c=='\n'){
    if(!discard&&used){
#ifdef WATCHER_LAN
     dispatch(watcher_lan::Source::usb,0,line,used,ms());
#else
     accept(line,used);
#endif
    }
#ifdef WATCHER_LAN
    watcher_lan::wipe(line,sizeof line);
#endif
    used=0;discard=false;
   }
   else if(c=='\r'){}else if(static_cast<unsigned char>(c)<32 || static_cast<unsigned char>(c)>126 || used==sizeof(line)){discard=true;}
   else if(!discard)line[used++]=c;
  }
  int64_t now=ms();
#ifdef WATCHER_LAN
  watcher_lan::wipe(bytes,sizeof bytes);provisioning.expire(boot_id,now);
  if(link_source==LinkSource::lan&&network.connection()!=link_generation)last_frame_ms=now-1501;
  expire_link(now);
  watcher_lan::Packet packet;
  for(unsigned i=0;i<8&&network.receive(packet);i++)dispatch(watcher_lan::Source::lan,packet.generation,packet.bytes,packet.size,packet.at);
  if(exit_requested&&network.stop()){
#ifdef WATCHER_FACTORY_FEEDBACK
   if(audio.deinit()){shutdown();clear_for_restart();esp_restart();}
#else
   shutdown();clear_for_restart();esp_restart();
#endif
  }
#endif
  now=ms(); // LAN dispatch may have accepted a W1 later than the pre-dispatch timestamp.
  expire_link(now);
#ifdef WATCHER_BOUNDED_RENDER
  bounded_observe(now);
#endif
#ifdef WATCHER_TOUCH_TIMING_DIAGNOSTICS
  touch_timing.phase(41,timing_begin,ms());timing_begin=ms();
#endif
  // Timing slot 42 remains reserved; do not shift the diagnostic wire layout.
  buttons(now);
#ifdef WATCHER_TOUCH_TIMING_DIAGNOSTICS
  touch_timing.phase(38,timing_begin,ms());timing_begin=ms();
#endif
#ifdef WATCHER_AUDIO_INIT_PROBE
#ifdef WATCHER_AUDIO_PATH_PROBE
  if((frame.flags&1)||button_kind==4)audio.stop();
#endif
  audio.tick(now); // Backend-enforced timeout: 15s init-only / 200ms silent path.
#endif
#if defined(WATCHER_COMPLETION_CHIME) && !defined(WATCHER_AUDIO_INIT_PROBE)
  audio.tick(now,frame.seq && now-last_frame_ms<=1500 && !(frame.flags&1) && button_kind!=4);
#endif
#ifdef WATCHER_TOUCH_TIMING_DIAGNOSTICS
  touch_timing.phase(39,timing_begin,ms());timing_begin=ms();
#endif
#ifdef WATCHER_AUTO_ROTATE
  const unsigned next_rotation=imu.update(now);
#ifdef WATCHER_BOUNDED_RENDER
  if(next_rotation!=bounded_rotation){bounded_rotation=next_rotation;bounded_observe(ms());}
#else
  if(next_rotation!=rotation){rotation=next_rotation;display.setRotation(rotation);}
#endif
#endif
#ifdef WATCHER_TOUCH_TIMING_DIAGNOSTICS
  touch_timing.phase(40,timing_begin,ms());
#endif
#ifdef WATCHER_TOUCH_SWIPE
#ifdef WATCHER_BOUNDED_RENDER
  now=ms(); // Fresh admission time after all earlier service work.
#endif
  touch(now); // One due transaction, after buttons/rotation; no catch-up reads.
#endif
  unsigned render_interval=50;
#if defined(WATCHER_GHOST_PAGES) && defined(WATCHER_TOUCH_SWIPE)
#ifdef WATCHER_AFTERLIFE
  if(desk.animations(frame.flags&8)&&((desk.open()&&desk.scrolling())||(!desk.open()&&((swipe.tracking()&&focus_context.matches(frame))||focus_transition.moving()||carousel.moving()))))render_interval=20;
#else
  if((swipe.tracking()&&focus_context.matches(frame)&&!(frame.flags&8))||focus_transition.moving()||carousel.moving())render_interval=20;
#endif
#endif
  #ifdef WATCHER_RETAINED_PAGING
  if(render_interval==20)render_interval=16;
#endif
  // Request a frame per sample interval during manipulation. Actual LCD cost
  // still bounds throughput; preserve the loop yield and bounded service budgets.
#ifdef WATCHER_BOUNDED_RENDER
  bounded_render(ms(),render_interval,rendered);
#if defined(WATCHER_RETAINED_PAGING) && defined(WATCHER_MOTION_METRICS)
  motion_report(ms());
#endif
#else
  if(now-rendered>=render_interval){
#ifdef WATCHER_TOUCH_TIMING_DIAGNOSTICS
   const auto began=ms();
#endif
   render(now);rendered=now;
#ifdef WATCHER_TOUCH_TIMING_DIAGNOSTICS
   touch_timing.render(began,ms());
#endif
  }
#endif
#ifdef WATCHER_TOUCH_USB_DIAGNOSTICS
  send_touch_diagnostic(ms()); // After buttons/render; fresh time, no wait, at most one snapshot/second.
#endif
  if(now-reported>=500){send_status();reported=now;}
  // At the audited 100 Hz tick rate, 5 ms truncates to zero. Always block
  // for at least one tick so IDLE0 can run and service its watchdog.
  vTaskDelay(pdMS_TO_TICKS(5)+1);
 }
}
