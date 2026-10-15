#pragma once
#if defined(ESP_PLATFORM) && defined(WATCHER_FEEDBACK_HOST_TEST)
#error "Host-only haptic test path must never enter a device build"
#endif
#include "factory-codec.h"
#include "esp_codec_dev.h"
#include "es8311_codec.h"
#include "motor-off.h"
#include <algorithm>
#include <cmath>
namespace watcher_feedback {
// All methods execute on ONE control owner. No I2C from timer/ISR/main-loop callbacks.
class Engine {
 public:
 using Clock=int64_t(*)();using Wait=void(*)(unsigned);using Allowed=bool(*)(void*);
 struct Result {uint32_t errors=0,sounds=0,pulses=0,dma_blocks=0;bool closed=true;};
 private:
 BoardIo io_;Clock clock_;Wait wait_;
 CodecControl control_;FixedI2s data_;
 const audio_codec_if_t* codec_=nullptr;esp_codec_dev_handle_t dev_=nullptr;
 bool powered_=false,initialized_=false,pa_configured_=false,motor_touched_=false,motor_low_verified_=false;
 Result result_{};
 bool pa(bool on){
  // Attempt both controls even if one fails. Published assembly uses alternate routes.
  const bool gpio=gpio_set_level(GPIO_NUM_14,on?1:0)==ESP_OK;
  const bool expander=io_.mask(0x06,2,on?2:0);
  if(!gpio||!expander)result_.errors|=1;
  return gpio&&expander;
 }
 bool check_codec(){
  if(control_.error)result_.errors|=2;
  if(data_.error)result_.errors|=4;
  return !result_.errors;
 }
 bool stop_motor(){
  if(!motor_touched_)return true;
  // On first contact establish the low GPIO fallback BEFORE relinquishing PWM.
  // During a pulse that fallback is already verified, so PWM-off is the first write.
  bool ok=!motor_low_verified_||io_.write(0x4f,0x1c,0);
  const bool off=watcher_motor::disable(
   [this](uint8_t reg,uint8_t& value){return io_.read(0x4f,reg,value);},
   [this](uint8_t reg,uint8_t value){return io_.write(0x4f,reg,value);});
  if(off)motor_low_verified_=true;
  ok=ok&&off;if(!ok)result_.errors|=8;return ok;
 }
 bool motor_prepare(){
  motor_touched_=true;
  if(!stop_motor())return false;
  for(uint8_t reg:{0x1e,0x20,0x22}){uint8_t value=0;if(!io_.read(0x4f,reg,value)||(value&0x80)){result_.errors|=8;return false;}}
  const uint8_t frequency[]={0x88,0x13};
  if(!io_.burst(0x4f,0x25,frequency,2)){result_.errors|=8;return false;}
  uint8_t lo=0,hi=0;
  if(!io_.read(0x4f,0x25,lo)||!io_.read(0x4f,0x26,hi)||lo!=0x88||hi!=0x13){result_.errors|=8;return false;}
  return true;
 }
 bool pause(unsigned ms,Allowed allowed,void* ctx){
  const auto end=clock_()+ms;
  while(clock_()<end){if(!allowed(ctx))return false;wait_(1);}
  return allowed(ctx);
 }
 bool prepare(Allowed allowed,void* ctx){
  if(initialized_)return check_codec();
  if(result_.errors||!allowed(ctx))return false;
  // Set latch low before making GPIO14 an output; do not use reset_pin's pull-up here.
  if(gpio_set_level(GPIO_NUM_14,0)!=ESP_OK||gpio_set_pull_mode(GPIO_NUM_14,GPIO_FLOATING)!=ESP_OK
     ||gpio_set_direction(GPIO_NUM_14,GPIO_MODE_OUTPUT)!=ESP_OK){result_.errors|=1;return false;}
  pa_configured_=true;
  if(!pa(false)||!io_.mask(0x14,2,0)||!io_.mask(0x04,0,2)||!io_.mask(0x13,4,0)||!io_.mask(0x03,0,4)){
   result_.errors|=1;return false;
  }
  // A failed rail write is ambiguous: mark it touched so cleanup still attempts off.
  powered_=true;result_.closed=false;
  if(!io_.mask(0x05,0,4)){result_.errors|=1;return false;}
  if(!pause(150,allowed,ctx))return false; // Deliberate startup preparation, outside tone duration.
  if(!data_.prepare()){check_codec();return false;}
  if(!pause(20,allowed,ctx))return false; // Zero-data clocks before the official codec sequence.
  control_.api.open(&control_.api,nullptr,0);
  es8311_codec_cfg_t cfg{};cfg.ctrl_if=&control_.api;cfg.codec_mode=ESP_CODEC_DEV_WORK_MODE_DAC;
  cfg.pa_pin=-1;cfg.use_mclk=true;cfg.mclk_div=256;
  codec_=es8311_codec_new(&cfg);
  if(!codec_||!check_codec()){result_.errors|=2;return false;}
  esp_codec_dev_cfg_t device{};device.dev_type=ESP_CODEC_DEV_TYPE_OUT;device.codec_if=codec_;device.data_if=&data_.api;
  dev_=esp_codec_dev_new(&device);
  if(!dev_){result_.errors|=2;return false;}
  esp_codec_dev_sample_info_t fs{};fs.bits_per_sample=16;fs.channel=1;fs.sample_rate=44100;fs.mclk_multiple=256;
  const int opened=esp_codec_dev_open(dev_,&fs);
  if(opened!=ESP_CODEC_DEV_OK||!check_codec()){result_.errors|=2;return false;}
  // Factory volume API and factory helper PCM scale, not direct gain-register guesses.
  const int gain=esp_codec_dev_set_out_vol(dev_,90);
  if(gain!=ESP_CODEC_DEV_OK||!check_codec()){result_.errors|=2;return false;}
  initialized_=true;return allowed(ctx);
 }
 bool write_samples(int16_t* samples,unsigned count){
  const int code=esp_codec_dev_write(dev_,samples,static_cast<int>(count*sizeof(int16_t)));
  if(code!=ESP_CODEC_DEV_OK)result_.errors|=4;
  return check_codec();
 }
 bool sound(Allowed allowed,void* ctx){
  if(!prepare(allowed,ctx))return false;
  if(!allowed(ctx)||!pa(true)||!pause(10,allowed,ctx))return false;
  constexpr unsigned first=4410,gap=882,second=5292,total=first+gap+second,tail=4410;
  int16_t pcm[256]{};
  for(unsigned base=0;base<total+tail;){
   if(!allowed(ctx))return false;
   const unsigned count=std::min(256u,total+tail-base);
   for(unsigned i=0;i<count;i++){
    const unsigned sample=base+i;unsigned pos=0,length=0;float hz=0;
    if(sample<first){pos=sample;length=first;hz=660;}
    else if(sample>=first+gap&&sample<total){pos=sample-first-gap;length=second;hz=880;}
    if(!length)pcm[i]=0;
    else{const float envelope=std::min(1.0f,std::min(pos/441.0f,(length-pos)/882.0f));
     pcm[i]=static_cast<int16_t>(3276.0f*envelope*std::sin(6.2831853f*hz*pos/44100.0f));}
   }
   if(!write_samples(pcm,count))return false;
   base+=count;
  }
  // write() means copied, not played. Wait beyond a full ring's EOFs after the last copy.
  const uint32_t baseline=data_.blocks();const auto end=clock_()+150;
  while(data_.blocks()-baseline<FixedI2s::descriptors+1){
   if(!allowed(ctx))return false;
   if(clock_()>=end){result_.errors|=16;return false;}
   wait_(1);
  }
  result_.dma_blocks=data_.blocks();
  if(!pa(false))return false;
  ++result_.sounds;return true;
 }
 bool pulse(Allowed allowed,void* ctx){
  if(!allowed(ctx)||result_.errors||!motor_prepare())return false;
  const uint8_t duty[]={0xb7,0x8e}; // Factory alarm: 92%, floor(4095*92/100)=3767.
  const auto stop_at=clock_()+70; // Armed before the possibly ambiguous enable transaction.
  if(!io_.burst(0x4f,0x1b,duty,2)){result_.errors|=8;stop_motor();return false;}
  uint8_t lo=0,hi=0;
  if(!io_.read(0x4f,0x1b,lo)||!io_.read(0x4f,0x1c,hi)||lo!=0xb7||hi!=0x8e){result_.errors|=8;stop_motor();return false;}
  while(clock_()<stop_at&&allowed(ctx))wait_(1);
  const bool permitted=allowed(ctx),stopped=stop_motor();
  if(permitted&&stopped){++result_.pulses;return true;}return false;
 }
 public:
 Engine(BoardIo io,Clock clock,Wait wait):io_(io),clock_(clock),wait_(wait),control_(io){}
 Result status()const{return result_;}
 // Called only by the owner; no buffering/replay here. Caller owns event freshness.
 bool run(bool with_sound,bool with_haptic,Allowed allowed,void* ctx){
  if(result_.errors||!allowed(ctx))return false;
  bool ok=true;
  if(with_sound)ok=sound(allowed,ctx);
  if(ok&&with_haptic){
#if defined(WATCHER_FEEDBACK_HOST_TEST) || defined(WATCHER_HAPTICS) || defined(WATCHER_FACTORY_HAPTIC_ACCEPTANCE)
   ok=pulse(allowed,ctx);
#else
   result_.errors|=8;ok=false; // Qualification hold: no device-callable activation path.
#endif
  }
  if(!ok){shutdown();return false;}
  return true;
 }
 bool shutdown(){
  bool safe=true;
  if(pa_configured_)safe=pa(false);
  const bool motor_off=stop_motor();safe=safe&&motor_off;
  // Official close/suspend while codec remains powered. Inspect sticky errors independently.
  if(dev_){esp_codec_dev_close(dev_);esp_codec_dev_delete(dev_);dev_=nullptr;}
  if(codec_){audio_codec_delete_codec_if(codec_);codec_=nullptr;}
  control_.api.close(&control_.api);
  initialized_=false;
  if(powered_){
   const bool quiet=data_.release();safe=safe&&quiet;
   if(quiet){const bool rail_off=io_.mask(0x05,4,0);safe=safe&&rail_off;if(rail_off)powered_=false;}
  }
  check_codec();
  if(!safe)result_.errors|=32;
  result_.closed=safe&&!powered_;result_.dma_blocks=data_.blocks();return result_.closed;
 }
};
}
