#pragma once
// Silent diagnostic: codec readback + DMA progress. PA is NEVER enabled.
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <initializer_list>
#include <memory>
#include <new>
#include <stdint.h>
namespace watcher_audio {
class PathProbe {
 using Mask=bool(*)(uint8_t,uint8_t,uint8_t,uint8_t);
 using Write=bool(*)(uint8_t,uint8_t,uint8_t);
 using Read=bool(*)(uint8_t,uint8_t,uint8_t&);
 using Clock=int64_t(*)();
 Mask mask_;Write write_;Read read_;Clock clock_;
 i2s_chan_handle_t tx_=nullptr;
 mutable portMUX_TYPE mux_=portMUX_INITIALIZER_UNLOCKED;
 uint32_t blocks_=0;
 bool attempted_=false,running_=false,closed_=false,completed_=false;
 int64_t deadline_=0;
 unsigned stage_=0,valid_=0,mismatch_=0,errors_=0;
 uint8_t values_[16]{};
 static bool sent(i2s_chan_handle_t,i2s_event_data_t*,void* user){
  auto* self=static_cast<PathProbe*>(user);
  portENTER_CRITICAL_ISR(&self->mux_);++self->blocks_;portEXIT_CRITICAL_ISR(&self->mux_);
  return false;
 }
 void sample(){
  static constexpr uint8_t regs[]={0x00,0x01,0x02,0x09,0x0d,0x12,0x13,0x31,0x32,0x37,0x13,0x03,0x05,0x14,0x04,0x06};
  static constexpr uint8_t masks[]={0,0xff,0xff,0,0xff,0xff,0xff,0,0xff,0xff,4,4,4,2,2,2};
  static constexpr uint8_t expected[]={0,0xb5,0x18,0,1,0,0x10,0,0xbf,8,0,4,4,0,2,0};
  valid_=mismatch_=0;
  for(unsigned i=0;i<16;i++){
   if(!read_(i<10?0x18:0x4f,regs[i],values_[i])){errors_|=4;continue;}
   valid_|=1u<<i;
   if((values_[i]&masks[i])!=expected[i])mismatch_|=1u<<i;
  }
  if(mismatch_)errors_|=8;
 }
 void finish(bool completed){
  if(!attempted_||closed_)return;
  deadline_=0;
  const bool pa_off=mask_(0x4f,0x06,2,0);bool quiet=true;
  if(running_&&tx_){if(i2s_channel_disable(tx_)==ESP_OK)running_=false;else quiet=false;}
  if(quiet&&tx_){if(i2s_del_channel(tx_)==ESP_OK)tx_=nullptr;else quiet=false;}
  if(quiet){for(auto pin:{GPIO_NUM_18,GPIO_NUM_17,GPIO_NUM_15,GPIO_NUM_21}){
   if(gpio_reset_pin(pin)!=ESP_OK)quiet=false;
   if(gpio_set_pull_mode(pin,GPIO_FLOATING)!=ESP_OK)quiet=false;
  }}
  const bool rail_off=quiet&&mask_(0x4f,0x05,4,0);
  closed_=pa_off&&quiet&&rail_off;if(!closed_)errors_|=512;
  completed_=completed&&closed_&&!errors_;
  if(completed_)stage_=6;
 }
 bool fail(unsigned error){errors_|=error;finish(false);return false;}
 public:
 PathProbe(Mask mask,Write write,Read read,Clock clock):mask_(mask),write_(write),read_(read),clock_(clock){}
 bool ready()const{return completed_;}bool fault()const{return errors_!=0;}
 unsigned stage()const{return stage_;}unsigned valid()const{return valid_;}
 unsigned mismatch()const{return mismatch_;}unsigned errors()const{return errors_;}
 uint32_t blocks()const{portENTER_CRITICAL(&mux_);const auto value=blocks_;portEXIT_CRITICAL(&mux_);return value;}
 const uint8_t* values()const{return values_;}
 void stop(){finish(false);}void deinit(){finish(false);}
 bool init(int64_t){
  if(attempted_)return false;
  attempted_=true;stage_=1;
  if(!mask_(0x4f,0x06,2,0)||!mask_(0x4f,0x05,4,0)
    ||!mask_(0x4f,0x13,4,0)||!mask_(0x4f,0x03,0,4)
    ||!mask_(0x4f,0x14,2,0)||!mask_(0x4f,0x04,0,2)
    ||!mask_(0x4f,0x05,0,4))return fail(1);
  vTaskDelay(pdMS_TO_TICKS(10)+1);
  static constexpr uint8_t codec[][2]={{0,0x80},{1,0xb5},{2,0x18},{0x0d,1},{0x12,0},{0x13,0x10},{0x32,0xbf},{0x37,8}};
  for(const auto& reg:codec){if(!write_(0x18,reg[0],reg[1]))return fail(2);vTaskDelay(pdMS_TO_TICKS(3)+1);}
  stage_=2;sample();if(errors_)return fail(0);
  i2s_chan_config_t channel=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,I2S_ROLE_MASTER);
  channel.dma_desc_num=24;channel.dma_frame_num=256;channel.auto_clear=true;
  stage_=3;if(i2s_new_channel(&channel,&tx_,nullptr)!=ESP_OK)return fail(16);
  i2s_std_config_t cfg{};cfg.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(44100);
  cfg.slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO);
  cfg.gpio_cfg.mclk=GPIO_NUM_18;cfg.gpio_cfg.bclk=GPIO_NUM_17;cfg.gpio_cfg.ws=GPIO_NUM_15;
  cfg.gpio_cfg.dout=GPIO_NUM_21;cfg.gpio_cfg.din=I2S_GPIO_UNUSED;
  if(i2s_channel_init_std_mode(tx_,&cfg)!=ESP_OK)return fail(16);
  i2s_event_callbacks_t callbacks{};callbacks.on_sent=sent;
  if(i2s_channel_register_event_callback(tx_,&callbacks,this)!=ESP_OK)return fail(64);
  std::unique_ptr<int16_t[]> zeros(new(std::nothrow) int16_t[6144*2]{});
  if(!zeros)return fail(32);
  size_t loaded=0;constexpr size_t bytes=6144*2*sizeof(int16_t);
  if(i2s_channel_preload_data(tx_,zeros.get(),bytes,&loaded)!=ESP_OK||loaded!=bytes)return fail(32);
  if(i2s_channel_enable(tx_)!=ESP_OK)return fail(128);
  running_=true;stage_=4;deadline_=clock_()+200;return true;
 }
 void tick(int64_t now){
  if(!deadline_||now<deadline_)return;
  stage_=5;sample();if(!blocks())errors_|=256;
  finish(true);
 }
};
}
