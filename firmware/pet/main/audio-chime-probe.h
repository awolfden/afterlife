#pragma once
// One supervised audible test per boot. No automatic completion subscription.
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cmath>
#include <initializer_list>
#include <memory>
#include <new>
#include <stdint.h>
namespace watcher_audio {
class ChimeProbe {
 using Mask=bool(*)(uint8_t,uint8_t,uint8_t,uint8_t);
 using Write=bool(*)(uint8_t,uint8_t,uint8_t);
 using Clock=int64_t(*)();
 Mask mask_;Write write_;Clock clock_;
 i2s_chan_handle_t tx_=nullptr;
 bool attempted_=false,running_=false,closed_=false,fault_=false,completed_=false;
 int64_t deadline_=0;
 static constexpr unsigned rate=44100,tone_frames=5292,buffer_frames=6144;
 void finish(bool completed){
  if(!attempted_||closed_)return;
  deadline_=0;
  const bool pa_off=mask_(0x4f,0x06,0x02,0);
  bool quiet=true;
  if(running_ && tx_){if(i2s_channel_disable(tx_)==ESP_OK)running_=false;else quiet=false;}
  if(quiet && tx_){if(i2s_del_channel(tx_)==ESP_OK)tx_=nullptr;else quiet=false;}
  if(quiet){
   for(auto pin:{GPIO_NUM_18,GPIO_NUM_17,GPIO_NUM_15,GPIO_NUM_21}){
    if(gpio_reset_pin(pin)!=ESP_OK)quiet=false;
    if(gpio_set_pull_mode(pin,GPIO_FLOATING)!=ESP_OK)quiet=false;
   }
  }
  // Retain codec power if clocks/pins could not be stopped, preventing back-powering.
  const bool rail_off=quiet && mask_(0x4f,0x05,0x04,0);
  closed_=pa_off&&quiet&&rail_off;
  fault_=fault_||!closed_;
  completed_=completed&&closed_&&!fault_;
 }
 bool fail(){fault_=true;finish(false);return false;}
 public:
 ChimeProbe(Mask mask,Write write,Clock clock):mask_(mask),write_(write),clock_(clock){}
 // Diagnostic bit4 means playback was submitted AND timed cleanup succeeded, not heard.
 bool ready()const{return completed_;}
 bool fault()const{return fault_;}
 void stop(){finish(false);}
 void deinit(){finish(false);}
 bool start(){
  if(attempted_)return false;
  attempted_=true;
  if(!mask_(0x4f,0x06,0x02,0)||!mask_(0x4f,0x05,0x04,0)
     ||!mask_(0x4f,0x13,0x04,0)||!mask_(0x4f,0x03,0,0x04)
     ||!mask_(0x4f,0x14,0x02,0)||!mask_(0x4f,0x04,0,0x02)
     ||!mask_(0x4f,0x05,0,0x04))return fail();
  vTaskDelay(pdMS_TO_TICKS(10)+1);
  // Vendor StopWatch sequence, but unity DAC gain (0xBF), not boosted 0xEF.
  static constexpr uint8_t codec[][2]={{0x00,0x80},{0x01,0xb5},{0x02,0x18},{0x0d,0x01},{0x12,0x00},{0x13,0x10},{0x32,0xbf},{0x37,0x08}};
  for(const auto& reg:codec){if(!write_(0x18,reg[0],reg[1]))return fail();vTaskDelay(pdMS_TO_TICKS(3)+1);}
  i2s_chan_config_t channel=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,I2S_ROLE_MASTER);
  channel.dma_desc_num=24;channel.dma_frame_num=256;channel.auto_clear=true;
  if(i2s_new_channel(&channel,&tx_,nullptr)!=ESP_OK)return fail();
  i2s_std_config_t cfg{};
  cfg.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(rate);
  cfg.slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO);
  cfg.gpio_cfg.mclk=GPIO_NUM_18;cfg.gpio_cfg.bclk=GPIO_NUM_17;cfg.gpio_cfg.ws=GPIO_NUM_15;
  cfg.gpio_cfg.dout=GPIO_NUM_21;cfg.gpio_cfg.din=I2S_GPIO_UNUSED;
  if(i2s_channel_init_std_mode(tx_,&cfg)!=ESP_OK)return fail();
  // Preload a finite tone + silence before TX/PA enable. No runtime PCM producer.
  std::unique_ptr<int16_t[]> pcm(new(std::nothrow) int16_t[buffer_frames*2]{});
  if(!pcm)return fail();
  for(unsigned s=0;s<tone_frames;s++){
   const float envelope=fminf(1.0f,fminf(s/441.0f,(tone_frames-s)/882.0f));
   const auto value=static_cast<int16_t>(1024.0f*envelope*sinf(6.2831853f*880.0f*s/rate));
   pcm[2*s]=pcm[2*s+1]=value;
  }
  size_t loaded=0;const size_t bytes=buffer_frames*2*sizeof(int16_t);
  if(i2s_channel_preload_data(tx_,pcm.get(),bytes,&loaded)!=ESP_OK||loaded!=bytes)return fail();
  if(i2s_channel_enable(tx_)!=ESP_OK)return fail();
  running_=true;
  if(!mask_(0x4f,0x06,0,0x02))return fail(); // PA enabled LAST.
  deadline_=clock_()+180; // 120ms note + trailing silence, bounded normal-run amp window.
  return true;
 }
 void tick(int64_t now,bool allowed){
  if(!deadline_)return;
  if(!allowed){finish(false);return;}
  if(now>=deadline_)finish(true);
 }
};
}
