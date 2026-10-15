#pragma once
// Explicit StopWatch-only audio seam. No M5Unified initialization, NVS or microphone.
// Wiring/codec sequence: M5Unified 0.2.19 StopWatch speaker callback.
#include "driver/i2s_std.h"
#include <cmath>
#include <stdint.h>

namespace watcher_audio {
class Audio {
 using Write=bool(*)(uint8_t,uint8_t,uint8_t);
 using Mask=bool(*)(uint8_t,uint8_t,uint8_t,uint8_t);
 Mask mask_;
 i2s_chan_handle_t tx_=nullptr;
 bool attempted_=false,healthy_=false,active_=false;
 static constexpr unsigned rate=44100;
#ifndef WATCHER_AUDIO_INIT_PROBE
 Write write_;
 unsigned sample_=0;
 int64_t deadline_=0;
 static constexpr unsigned total=5292; // 120 ms, finite PCM only.
#endif
 bool power_off(){
  const bool pa=mask_(0x4f,0x06,0x02,0); // M5IOE1 gpio10 is zero-based pin 9.
  const bool rail=mask_(0x4f,0x05,0x04,0); // gpio3 is zero-based pin 2.
  return pa&&rail;
 }
 public:
#ifdef WATCHER_AUDIO_INIT_PROBE
 Audio(Write,Mask mask):mask_(mask){}
#else
 Audio(Write write,Mask mask):mask_(mask),write_(write){}
#endif
 bool ready()const{return healthy_;}
 void stop(){
  if(!attempted_)return; // Dormant startup must not touch hardware.
  const bool off=power_off();
  if(active_ && tx_ && i2s_channel_disable(tx_)!=ESP_OK)healthy_=false;
  active_=false;if(!off)healthy_=false;
 }
 void deinit(){
  if(!attempted_)return;
  stop();
  if(tx_ && i2s_del_channel(tx_)==ESP_OK)tx_=nullptr;
  healthy_=false; // One initialization attempt per boot, including failure/disconnect.
 }
 bool init(){
  if(attempted_)return healthy_;
  attempted_=true;
  // Configure low output latches before push-pull/output mode. Never touches motor pin 8.
  if(!power_off() || !mask_(0x4f,0x13,0x04,0) || !mask_(0x4f,0x03,0,0x04)
     || !mask_(0x4f,0x14,0x02,0) || !mask_(0x4f,0x04,0,0x02))return false;
  i2s_chan_config_t channel=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,I2S_ROLE_MASTER);
  channel.dma_desc_num=4;channel.dma_frame_num=256;channel.auto_clear=true;
  if(i2s_new_channel(&channel,&tx_,nullptr)!=ESP_OK)return false;
  i2s_std_config_t cfg{};
  cfg.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(rate);
  cfg.slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO);
  cfg.gpio_cfg.mclk=GPIO_NUM_18;cfg.gpio_cfg.bclk=GPIO_NUM_17;cfg.gpio_cfg.ws=GPIO_NUM_15;
  cfg.gpio_cfg.dout=GPIO_NUM_21;cfg.gpio_cfg.din=I2S_GPIO_UNUSED;
  if(i2s_channel_init_std_mode(tx_,&cfg)!=ESP_OK){i2s_del_channel(tx_);tx_=nullptr;return false;}
  healthy_=true;return true;
 }
#ifndef WATCHER_AUDIO_INIT_PROBE
 void start(int64_t now){
  if(!healthy_||active_)return;
  // DAC unity gain, unlike upstream's boosted 0xEF. PCM peak is only 512/32767.
  static constexpr uint8_t codec[][2]={{0x00,0x80},{0x01,0xb5},{0x02,0x18},{0x0d,0x01},{0x12,0x00},{0x13,0x10},{0x32,0xbf},{0x37,0x08}};
  bool ok=mask_(0x4f,0x05,0,0x04);
  vTaskDelay(pdMS_TO_TICKS(10));
  for(const auto& reg:codec){if(!ok)break;ok=write_(0x18,reg[0],reg[1]);vTaskDelay(pdMS_TO_TICKS(3));}
  if(ok)ok=i2s_channel_enable(tx_)==ESP_OK;
  if(ok){active_=true;ok=mask_(0x4f,0x06,0,0x02);}
  if(!ok){healthy_=false;stop();return;}
  sample_=0;deadline_=now+220;
 }
 void tick(int64_t now,bool allowed){
  if(!active_)return;
  if(!allowed||now>=deadline_){stop();return;}
  if(sample_>=total)return; // DMA drains its finite samples before power-off deadline.
  int16_t pcm[512];const unsigned count=total-sample_<256?total-sample_:256;
  for(unsigned i=0;i<count;i++){
   const unsigned s=sample_+i;
   const float envelope=fminf(1.0f,fminf(s/441.0f,(total-s)/882.0f));
   const auto value=static_cast<int16_t>(512.0f*envelope*sinf(6.2831853f*880.0f*s/rate));
   pcm[2*i]=pcm[2*i+1]=value;
  }
  size_t written=0;
  const size_t bytes=count*2*sizeof(int16_t);
  if(i2s_channel_write(tx_,pcm,bytes,&written,10)!=ESP_OK||written!=bytes){healthy_=false;stop();return;}
  sample_+=count;
 }
#endif // Silent probe image has no codec-enable or PCM-playback methods.
};
}
