#pragma once
// Silent one-shot diagnostic. No codec writes, PA-enable path, PCM, or TX-enable API.
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdint.h>
#include <initializer_list>
namespace watcher_audio {
class InitProbe {
 using Mask=bool(*)(uint8_t,uint8_t,uint8_t,uint8_t);
 Mask mask_;
 i2s_chan_handle_t tx_=nullptr;
 bool attempted_=false,healthy_=false,fault_=false,closed_=false;
 int64_t deadline_=0;
 public:
 explicit InitProbe(Mask mask):mask_(mask){}
 bool ready()const{return healthy_;}
 bool fault()const{return fault_;}
 void deinit(){
  if(!attempted_||closed_)return;
  healthy_=false;deadline_=0;
  const bool pa_off=mask_(0x4f,0x06,0x02,0);
  bool quiet=true;
  // Delete READY channel: SDK disables MCLK and releases DMA before returning.
  if(tx_){if(i2s_del_channel(tx_)==ESP_OK)tx_=nullptr;else quiet=false;}
  if(quiet){
   // Revoke routing and pulls, not just the SDK reservation, before rail removal.
   for(auto pin:{GPIO_NUM_18,GPIO_NUM_17,GPIO_NUM_15,GPIO_NUM_21}){
    if(gpio_reset_pin(pin)!=ESP_OK)quiet=false;
    if(gpio_set_pull_mode(pin,GPIO_FLOATING)!=ESP_OK)quiet=false;
   }
  }
  // On cleanup failure retain codec power (PA still off), never clock an unpowered chip.
  const bool rail_off=quiet && mask_(0x4f,0x05,0x04,0);
  fault_=!pa_off||!quiet||!rail_off;
  closed_=!fault_;
 }
 void stop(){deinit();}
 bool init(int64_t now){
  if(attempted_)return healthy_;
  attempted_=true;deadline_=now+15000;
  // Output latch low before mode changes. No motor pin (pin8) writes.
  if(!mask_(0x4f,0x06,0x02,0)||!mask_(0x4f,0x05,0x04,0)
     ||!mask_(0x4f,0x13,0x04,0)||!mask_(0x4f,0x03,0,0x04)
     ||!mask_(0x4f,0x14,0x02,0)||!mask_(0x4f,0x04,0,0x02)
     ||!mask_(0x4f,0x05,0,0x04)){deinit();return false;}
  // Match vendor rail-first order. Round UP so tick conversion cannot shorten 10 ms.
  vTaskDelay(pdMS_TO_TICKS(10)+1);
  i2s_chan_config_t channel=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,I2S_ROLE_MASTER);
  channel.dma_desc_num=4;channel.dma_frame_num=256;channel.auto_clear=true;
  if(i2s_new_channel(&channel,&tx_,nullptr)!=ESP_OK){deinit();return false;}
  i2s_std_config_t cfg{};
  cfg.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(44100);
  cfg.slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO);
  cfg.gpio_cfg.mclk=GPIO_NUM_18;cfg.gpio_cfg.bclk=GPIO_NUM_17;cfg.gpio_cfg.ws=GPIO_NUM_15;
  cfg.gpio_cfg.dout=GPIO_NUM_21;cfg.gpio_cfg.din=I2S_GPIO_UNUSED;
  if(i2s_channel_init_std_mode(tx_,&cfg)!=ESP_OK){deinit();return false;}
  healthy_=true;return true;
 }
 void tick(int64_t now){if(deadline_ && now>=deadline_)deinit();}
};
}
