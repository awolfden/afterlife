#include "../firmware/pet/main/factory-feedback-service.h"
#include <array>
#include <climits>
#include <cstdio>
using namespace watcher_feedback;
using namespace factory_fake;
static std::array<uint8_t,256> ioe{},codec{};
static void(*register_hook)()=nullptr;
static int io_calls=0,fail_at=0;static bool fail_codec=false;static int64_t cancel_at=INT64_MAX,motor_on=-1,motor_off=-1;
static bool fault_io(){return ++io_calls==fail_at;}
static bool read_reg(uint8_t address,uint8_t reg,uint8_t& value){
 assert(address==0x18||address==0x4f);
 if(fault_io()||(fail_codec&&address==0x18))return false;
 if(address==0x18){assert(rail);value=codec[reg];}
 else {value=ioe[reg];if(reg==8)value=(ioe[0x1c]&0x80)?((ioe[0x1b]||(ioe[0x1c]&15))?1:0):(ioe[6]&1);}
 return true;
}
static bool write_reg(uint8_t address,uint8_t reg,uint8_t value){
 assert(address==0x18||address==0x4f);
 if(fault_io()||(fail_codec&&address==0x18))return false;
 if(address==0x18){assert(rail);codec[reg]=value;trace.push_back("codec");return true;}
 if(reg==5){rail=(value&4)!=0;assert(rail||!running);trace.push_back(rail?"rail-on":"rail-off");}
 if(reg==6){const bool on=(value&2)!=0;if(on&&!pa)pa_on=clock_value;pa=on;trace.push_back(on?"pa-on":"pa-off");}
 if(reg==0x1c){if(value&0x80){motor_on=clock_value;trace.push_back("motor-on");}else {motor_off=clock_value;trace.push_back("motor-off");}}
 assert(reg<0x1d||reg>0x22); // Never modify any other PWM channel.
 ioe[reg]=value;if(register_hook)register_hook();return true;
}
static bool burst(uint8_t address,uint8_t reg,const uint8_t* bytes,size_t count){
 assert(address==0x4f&&count==2&&(reg==0x25||reg==0x1b));
 if(reg==0x1b)assert(bytes[0]==0xb7&&bytes[1]==0x8e);
 trace.push_back(reg==0x25?"frequency-pair":"duty-pair");
 return write_reg(address,reg,bytes[0])&&write_reg(address,reg+1,bytes[1]);
}
static BoardIo board(){return {read_reg,write_reg,burst};}
static int64_t clock_ms(){return clock_value;}
static void wait_ms(unsigned){clock_value+=10;progress();}
static bool allowed(void*){return clock_value<cancel_at;}
static void reset_all(){factory_fake::reset();ioe.fill(0);codec.fill(0);io_calls=fail_at=0;fail_codec=false;cancel_at=INT64_MAX;motor_on=motor_off=-1;tasks_created=0;task_fail=false;delay_hook=nullptr;register_hook=nullptr;}
static Service* active_service=nullptr;
static void cancel_and_unmute(){write_hook=nullptr;active_service->stop();active_service->context(clock_value+1500,true);}
static void pump_cleanup(){delay_hook=nullptr;active_service->process_for_test();}
static void pump_repeatedly(){active_service->process_for_test();}
static void resume_during_shutdown(){register_hook=nullptr;active_service->context(clock_value+1500,true);assert(active_service->start(clock_value));}
static void close_service(Service& service){active_service=&service;delay_hook=pump_cleanup;assert(service.deinit());delay_hook=nullptr;assert(!rail&&!running&&!pa);} 
static size_t index(const char* s){for(size_t i=0;i<trace.size();i++)if(trace[i]==s)return i;assert(false);return 0;}
int main(){
 unsigned cases=0;
 // Adapter errors remain observable even when the upstream caller discards a return.
 reset_all();{
  CodecControl ctrl(board());assert(!ctrl.api.is_open(&ctrl.api));uint8_t value=0;
  assert(ctrl.api.read_reg(&ctrl.api,9,1,&value,1)!=0);assert(ctrl.error);ctrl.error=0;
  ctrl.api.open(&ctrl.api,nullptr,0);rail=true;value=12;
  assert(ctrl.api.write_reg(&ctrl.api,9,1,&value,1)==0);value=0;assert(ctrl.api.read_reg(&ctrl.api,9,1,&value,1)==0&&value==12);
  assert(ctrl.api.write_reg(&ctrl.api,9,2,&value,1)!=0);const int saved=ctrl.error;
  assert(ctrl.api.read_reg(&ctrl.api,9,1,&value,1)==0&&ctrl.error==saved);ctrl.api.close(&ctrl.api);++cases;
 }
 reset_all();{
  rail=true;FixedI2s data;assert(data.prepare());esp_codec_dev_sample_info_t fs{};fs.sample_rate=44100;fs.bits_per_sample=16;fs.channel=1;
  assert(data.api.set_fmt(&data.api,ESP_CODEC_DEV_TYPE_OUT,&fs)==0);
  for(unsigned field=0;field<5;field++){auto bad=fs;if(field==0)bad.sample_rate=48000;if(field==1)bad.bits_per_sample=24;if(field==2)bad.channel=2;if(field==3)bad.channel_mask=2;if(field==4)bad.mclk_multiple=384;assert(data.api.set_fmt(&data.api,ESP_CODEC_DEV_TYPE_OUT,&bad)!=0);}
  assert(data.release());++cases;
 }
 reset_all();{
  Engine engine(board(),clock_ms,wait_ms);assert(!rail&&!running&&!pa&&trace.empty());
  assert(engine.run(true,false,allowed,nullptr));assert(engine.status().sounds==1&&!engine.status().errors&&nonzero>0);
  assert(codec[1]==0x3f&&codec[2]==0&&((codec[9]&0x1c)==0x0c));
  assert(codec[0x32]==0xbc); // Pinned driver, volume90: +10 half-dB steps over volume80 (0xb2).
  assert(index("rail-on")<index("zero-preload")&&index("zero-preload")<index("clocks-on")&&index("clocks-on")<index("codec")&&index("codec")<index("pa-on")&&index("pa-on")<index("pcm"));
  const auto first_allocations=allocations;assert(engine.run(true,false,allowed,nullptr));assert(allocations==first_allocations&&engine.status().sounds==2);
  assert(engine.shutdown());assert(!rail&&!running&&!pa);assert(index("clocks-off")<index("delete")&&index("delete")<index("float")&&index("float")<index("rail-off"));++cases;
 }
 for(unsigned mode=0;mode<4;mode++){
  reset_all();Engine engine(board(),clock_ms,wait_ms);
  if(mode==0)short_write=true;if(mode==1)write_timeout=true;if(mode==2)no_dma=true;if(mode==3)fail_codec=true;
  assert(!engine.run(true,false,allowed,nullptr));assert(engine.status().errors&&!rail&&!running&&!pa);assert(engine.status().sounds==0);
  assert(!engine.run(true,false,allowed,nullptr));++cases;
 }
 for(int64_t at:{0,100,185,500}){
  reset_all();Engine engine(board(),clock_ms,wait_ms);cancel_at=at;
  assert(!engine.run(true,false,allowed,nullptr));assert(!pa&&!rail&&!running&&engine.status().sounds==0);++cases;
 }
 for(unsigned mode=0;mode<3;mode++){
  reset_all();Engine engine(board(),clock_ms,wait_ms);assert(engine.run(true,false,allowed,nullptr));
  if(mode==0)fail_disable=true;if(mode==1)fail_delete=true;if(mode==2)fail_pins=true;
  assert(!engine.shutdown());assert(rail&&engine.status().errors&&!engine.status().closed);
  fail_disable=fail_delete=fail_pins=false;assert(engine.shutdown());assert(!rail&&!running);++cases;
 }
 reset_all();{
  Engine engine(board(),clock_ms,wait_ms);assert(engine.run(false,true,allowed,nullptr));assert(engine.status().pulses==1&&motor_on>=0&&motor_off-motor_on==70);assert(ioe[0x25]==0x88&&ioe[0x26]==0x13&&ioe[0x1b]==0&&ioe[0x1c]==0);assert(!rail&&!running);assert(index("frequency-pair")<index("duty-pair"));assert(engine.shutdown());++cases;
 }
 reset_all();{
  Engine engine(board(),clock_ms,wait_ms);cancel_at=30;
  assert(!engine.run(false,true,allowed,nullptr));assert(motor_on==0&&motor_off==30);
  assert(!engine.status().errors&&engine.status().pulses==0&&ioe[0x1c]==0&&!(ioe[6]&1));++cases;
 }
 for(uint8_t reg:{0x1e,0x20,0x22}){
  reset_all();ioe[reg]=0x80;Engine engine(board(),clock_ms,wait_ms);
  assert(!engine.run(false,true,allowed,nullptr));assert(engine.status().errors&&motor_on==-1&&ioe[0x25]==0);++cases;
 }
 // Inject every individual IO failure across the complete haptic path, including
 // an ambiguous paired enable and disable/readback. No retry may re-enable PWM.
 reset_all();int total=0;{Engine engine(board(),clock_ms,wait_ms);assert(engine.run(false,true,allowed,nullptr));total=io_calls;engine.shutdown();}
 for(int failure=1;failure<=total;failure++){
  reset_all();fail_at=failure;Engine engine(board(),clock_ms,wait_ms);
  assert(!engine.run(false,true,allowed,nullptr));assert(engine.status().errors&&engine.status().pulses==0);
  const auto attempts=motor_on;assert(!engine.run(false,true,allowed,nullptr));assert(motor_on==attempts);engine.shutdown();assert(ioe[0x1c]==0&&!(ioe[6]&1));++cases;
 }
 reset_all();{
  ioe[6]=2;pa=true;Engine engine(board(),clock_ms,wait_ms);assert(engine.run(false,true,allowed,nullptr));assert((ioe[6]&2)==2);engine.shutdown();pa=false;++cases;
 }
 reset_all();{
  assert(tick_wait_budget(10,9)<6); // A one-tick wait can lose the next DMA EOF.
  for(int phase=0;phase<10;++phase)assert(tick_wait_budget(FixedI2s::write_timeout_ms,phase)>=6);
  tick_granularity=true;Engine engine(board(),clock_ms,wait_ms);
  assert(engine.run(true,false,allowed,nullptr));assert(engine.status().sounds==1&&!engine.status().errors);assert(engine.shutdown());++cases;
 }
 for(unsigned failure=1;failure<=5;failure++){
  reset_all();fail_setup=failure;Engine engine(board(),clock_ms,wait_ms);
  assert(!engine.run(true,false,allowed,nullptr));assert(engine.status().errors&&!pa&&!running&&!rail);++cases;
 }
 reset_all();{
  HapticGate gate;watcher_wire::Frame f{1,1,0,1,3,1,0,1,1,0};
  assert(!gate.take(f));f.flags=0;++f.seq;assert(!gate.take(f));++f.alert;assert(gate.take(f));assert(!gate.take(f));
  ++f.alert;f.state=8;assert(!gate.take(f));f.state=3;assert(!gate.take(f));++f.alert;f.other=1;f.state=0;assert(gate.take(f));++cases;
 }
 reset_all();{
  Service service(board());service.init();service.init();assert(tasks_created==1&&trace.empty());assert(service.ready());
  service.context(1500,true);assert(service.start(0));assert(!service.start(0));service.stop();service.context(1500,true);
  service.process_for_test();assert(trace.empty()&&service.status().sounds==0&&service.status().pulses==0);close_service(service);++cases;
 }
 reset_all();{
  Service service(board());service.init();service.context(10,true);assert(service.start(0));clock_value=20;service.process_for_test();assert(trace.empty());
  service.context(1500,true);service.process_for_test();assert(trace.empty());close_service(service);++cases;
 }
 reset_all();{
  Service service(board());active_service=&service;service.init();service.context(1500,true);write_hook=cancel_and_unmute;
  assert(service.start(0));service.process_for_test();assert(service.status().sounds==0&&service.status().pulses==0&&!rail&&!running&&!pa);
  assert(service.start(clock_value));service.process_for_test();assert(service.status().sounds==1&&service.status().pulses==1);
#ifdef WATCHER_FACTORY_ACCEPTANCE
  assert(service.status().closed&&!rail&&!running); // Same playback service, automatic acceptance cleanup.
#else
  assert(!service.status().closed&&rail&&running); // Production service stays warm, PA off.
#endif
  close_service(service);assert(!service.start(clock_value));++cases;
 }
 reset_all();{
  Service service(board());service.init();service.context(1500,true);watcher_wire::Frame f{1,1,0,1,3,1,0,0,1,0};
#ifdef WATCHER_FACTORY_ACCEPTANCE
  assert(!service.alert(f));service.process_for_test();assert(trace.empty());
#else
  assert(service.alert(f));service.process_for_test();assert(service.status().pulses==1&&service.status().sounds==0&&allocations==0);
  assert(!service.alert(f));++f.alert;f.flags=1;service.context(1500,false);assert(!service.alert(f));f.flags=0;service.context(1500,true);assert(!service.alert(f));
  ++f.alert;assert(service.alert(f));service.stop();service.context(1500,true);service.process_for_test();assert(service.status().pulses==1);assert(!service.alert(f));
  ++f.alert;assert(service.alert(f));service.process_for_test();assert(service.status().pulses==2);
#endif
  close_service(service);++cases;
 }
 reset_all();{
  task_fail=true;Service service(board());service.init();assert(!service.ready()&&service.fault());service.context(1500,true);assert(!service.start(0)&&trace.empty());assert(service.deinit());++cases;
 }
 reset_all();{
  Service service(board());service.init();service.context(200,true);assert(service.start(0));service.process_for_test();
  assert(service.status().sounds==0&&!service.status().errors&&!rail&&!running&&!pa);
  service.context(clock_value+1500,true);assert(service.start(clock_value));service.process_for_test();assert(service.status().sounds==1);close_service(service);++cases;
 }
#ifndef WATCHER_FACTORY_ACCEPTANCE
 reset_all();{
  Service service(board());active_service=&service;service.init();service.context(1500,true);assert(service.start(0));service.process_for_test();
  service.stop();register_hook=resume_during_shutdown;service.process_for_test();
  assert(!register_hook&&!rail&&allocations==1&&service.status().closed&&service.status().sounds==1);
  service.process_for_test();assert(allocations==2&&service.status().sounds==2);close_service(service);++cases;
 }
 reset_all();{
  Service service(board());active_service=&service;service.init();service.context(1500,true);assert(service.start(0));service.process_for_test();
  fail_pins=true;delay_hook=pump_repeatedly;assert(!service.deinit());assert(rail&&!service.ready()&&!service.status().closed);
  fail_pins=false;assert(service.deinit());delay_hook=nullptr;assert(!rail&&!running&&service.status().closed);++cases;
 }
#endif
 // Independent preference masks exercise the existing real engine and owner.
 for(unsigned mask=0;mask<4;++mask){reset_all();Service service(board());service.init();assert(service.outputs(mask));service.context(1500,true);
  assert(service.start(0)==(mask!=0));service.process_for_test();assert(service.status().sounds==((mask&1)?1u:0u));assert(service.status().pulses==((mask&2)?1u:0u));close_service(service);++cases;
 }
 reset_all();{Service service(board());service.init();service.context(1500,true);assert(service.start(0));assert(service.outputs(0));service.context(1500,true);service.process_for_test();assert(service.status().sounds==0&&service.status().pulses==0);assert(!service.start(0));assert(!service.outputs(4));close_service(service);++cases;}
#ifndef WATCHER_FACTORY_ACCEPTANCE
 for(unsigned mask=0;mask<4;++mask){reset_all();Service service(board());service.init();service.outputs(mask);service.context(1500,true);watcher_wire::Frame f{1,1,0,1,3,1,0,0,1,0};
  assert(service.alert(f,true,mask)==(mask!=0));service.process_for_test();assert(service.status().sounds==((mask&1)?1u:0u));assert(service.status().pulses==((mask&2)?1u:0u));assert(!service.alert(f,true,3));close_service(service);++cases;
 }
#endif
 printf("factory feedback: %u scenarios passed (real pinned codec + mocked hardware; no physical proof)\n",cases);
}
