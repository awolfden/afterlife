#pragma once
// Checked output-only adapters for the unmodified, pinned ES8311 driver. Owner-task only.
#include "audio_codec_ctrl_if.h"
#include "audio_codec_data_if.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include <stddef.h>
#include <stdint.h>
#include <initializer_list>
#include <type_traits>
namespace watcher_feedback {
struct BoardIo {
 bool (*read)(uint8_t,uint8_t,uint8_t&);
 bool (*write)(uint8_t,uint8_t,uint8_t);
 bool (*burst)(uint8_t,uint8_t,const uint8_t*,size_t);
 // One owner serializes the ENTIRE read-modify-write, not just each bus call.
 bool mask(uint8_t reg,uint8_t clear,uint8_t set)const{
  uint8_t value=0;return read(0x4f,reg,value)&&write(0x4f,reg,(value&~clear)|set);
 }
};
struct CodecControl {
 audio_codec_ctrl_if_t api{}; // First member; never pass this stack adapter to delete_ctrl_if.
 BoardIo io;
 bool opened=false;
 int error=0;
 static CodecControl& self(const audio_codec_ctrl_if_t* p){return *reinterpret_cast<CodecControl*>(const_cast<audio_codec_ctrl_if_t*>(p));}
 int fail(int value){if(!error)error=value;return value;}
 explicit CodecControl(BoardIo board):io(board){
  api.open=[](const audio_codec_ctrl_if_t* p,void*,int){auto& s=self(p);if(!s.io.read||!s.io.write)return s.fail(ESP_CODEC_DEV_INVALID_ARG);s.opened=true;return ESP_CODEC_DEV_OK;};
  api.is_open=[](const audio_codec_ctrl_if_t* p){return self(p).opened;};
  api.close=[](const audio_codec_ctrl_if_t* p){self(p).opened=false;return ESP_CODEC_DEV_OK;};
  api.read_reg=[](const audio_codec_ctrl_if_t* p,int reg,int reg_len,void* data,int len){
   auto& s=self(p);
   if(!s.opened||reg_len!=1||len!=1||reg<0||reg>255||!data)return s.fail(ESP_CODEC_DEV_INVALID_ARG);
   return s.io.read(0x18,static_cast<uint8_t>(reg),*static_cast<uint8_t*>(data))?ESP_CODEC_DEV_OK:s.fail(ESP_CODEC_DEV_READ_FAIL);
  };
  api.write_reg=[](const audio_codec_ctrl_if_t* p,int reg,int reg_len,void* data,int len){
   auto& s=self(p);
   if(!s.opened||reg_len!=1||len!=1||reg<0||reg>255||!data)return s.fail(ESP_CODEC_DEV_INVALID_ARG);
   return s.io.write(0x18,static_cast<uint8_t>(reg),*static_cast<uint8_t*>(data))?ESP_CODEC_DEV_OK:s.fail(ESP_CODEC_DEV_WRITE_FAIL);
  };
 }
};
static_assert(std::is_standard_layout<CodecControl>::value && offsetof(CodecControl,api)==0,"Codec adapter layout");

struct FixedI2s {
 audio_codec_data_if_t api{};
 i2s_chan_handle_t tx=nullptr;
 bool opened=false,running=false;
 int error=0;
 mutable portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
 uint32_t completed_blocks=0;
 uint32_t submitted_bytes=0;
 static constexpr unsigned descriptors=6,frames_per_block=256;
 // IDF converts each semaphore/queue wait to whole RTOS ticks. At 100 Hz a
 // 10ms timeout can expire before the next 5.8ms DMA block reaches EOF.
 static constexpr uint32_t write_timeout_ms=60;
 static_assert(write_timeout_ms>=3*portTICK_PERIOD_MS+6,"TX wait needs tick and DMA-block margin");
 static FixedI2s& self(const audio_codec_data_if_t* p){return *reinterpret_cast<FixedI2s*>(const_cast<audio_codec_data_if_t*>(p));}
 int fail(int value){if(!error)error=value;return value;}
 static bool on_sent(i2s_chan_handle_t,i2s_event_data_t*,void* ctx){
  auto& s=*static_cast<FixedI2s*>(ctx);portENTER_CRITICAL_ISR(&s.mux);++s.completed_blocks;portEXIT_CRITICAL_ISR(&s.mux);return false;
 }
 uint32_t blocks()const{portENTER_CRITICAL(&mux);const auto n=completed_blocks;portEXIT_CRITICAL(&mux);return n;}
 FixedI2s(){
  api.open=[](const audio_codec_data_if_t* p,void*,int){return self(p).opened?ESP_CODEC_DEV_OK:ESP_CODEC_DEV_WRONG_STATE;};
  api.is_open=[](const audio_codec_data_if_t* p){return self(p).opened;};
  api.set_fmt=[](const audio_codec_data_if_t* p,esp_codec_dev_type_t type,esp_codec_dev_sample_info_t* fs){
   auto& s=self(p);
   if(!s.opened||type!=ESP_CODEC_DEV_TYPE_OUT||!fs||fs->sample_rate!=44100||fs->bits_per_sample!=16||fs->channel!=1
      ||(fs->channel_mask!=0&&fs->channel_mask!=1)||(fs->mclk_multiple!=0&&fs->mclk_multiple!=256))return s.fail(ESP_CODEC_DEV_NOT_SUPPORT);
   return ESP_CODEC_DEV_OK; // Fixed hardware format is already established; never reconfigure live TX.
  };
  api.enable=[](const audio_codec_data_if_t* p,esp_codec_dev_type_t type,bool enable){
   auto& s=self(p);
   if(!s.tx||type!=ESP_CODEC_DEV_TYPE_OUT)return s.fail(ESP_CODEC_DEV_WRONG_STATE);
   if(s.running==enable)return ESP_CODEC_DEV_OK;
   const auto result=enable?i2s_channel_enable(s.tx):i2s_channel_disable(s.tx);
   if(result!=ESP_OK)return s.fail(result);
   s.running=enable;return ESP_CODEC_DEV_OK;
  };
  // The vendor interface requires a read callback; it cannot access hardware.
  api.read=[](const audio_codec_data_if_t* p,uint8_t*,int){return self(p).fail(ESP_CODEC_DEV_NOT_SUPPORT);};
  api.write=[](const audio_codec_data_if_t* p,uint8_t* data,int bytes){
   auto& s=self(p);
   if(!s.running||!data||bytes<=0||bytes>512||(bytes&1))return s.fail(ESP_CODEC_DEV_INVALID_ARG);
   size_t written=0;const auto result=i2s_channel_write(s.tx,data,static_cast<size_t>(bytes),&written,write_timeout_ms);
   s.submitted_bytes+=static_cast<uint32_t>(written);
   if(result!=ESP_OK)return s.fail(result);
   return written==static_cast<size_t>(bytes)?ESP_CODEC_DEV_OK:s.fail(ESP_CODEC_DEV_WRITE_FAIL);
  };
  api.close=[](const audio_codec_data_if_t* p){return self(p).release()?ESP_CODEC_DEV_OK:ESP_CODEC_DEV_DRV_ERR;};
 }
 bool prepare(){
  if(tx||error)return false;
  i2s_chan_config_t channel=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,I2S_ROLE_MASTER);
  channel.dma_desc_num=descriptors;channel.dma_frame_num=frames_per_block;channel.auto_clear=true;
  if(const auto result=i2s_new_channel(&channel,&tx,nullptr);result!=ESP_OK){fail(result);return false;}
  i2s_std_config_t cfg{};cfg.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(44100u);
  cfg.clk_cfg.mclk_multiple=I2S_MCLK_MULTIPLE_256;
  cfg.slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_MONO);
  cfg.slot_cfg.slot_mask=I2S_STD_SLOT_LEFT;
  cfg.gpio_cfg.mclk=GPIO_NUM_18;cfg.gpio_cfg.bclk=GPIO_NUM_17;cfg.gpio_cfg.ws=GPIO_NUM_15;
  cfg.gpio_cfg.dout=GPIO_NUM_21;cfg.gpio_cfg.din=I2S_GPIO_UNUSED;
  if(const auto result=i2s_channel_init_std_mode(tx,&cfg);result!=ESP_OK){fail(result);return false;}
  i2s_event_callbacks_t callbacks{};callbacks.on_sent=on_sent;
  if(const auto result=i2s_channel_register_event_callback(tx,&callbacks,this);result!=ESP_OK){fail(result);return false;}
  const int16_t zeros[descriptors*frames_per_block]{};
  size_t loaded=0;const auto result=i2s_channel_preload_data(tx,zeros,sizeof zeros,&loaded);
  if(result!=ESP_OK||loaded!=sizeof zeros){fail(result!=ESP_OK?result:ESP_CODEC_DEV_WRITE_FAIL);return false;}
  opened=true;return api.enable(&api,ESP_CODEC_DEV_TYPE_OUT,true)==ESP_CODEC_DEV_OK;
 }
 bool release(){
  bool quiet=true;
  if(quiet&&running&&tx){if(i2s_channel_disable(tx)==ESP_OK)running=false;else quiet=false;}
  if(quiet&&tx){if(i2s_del_channel(tx)==ESP_OK)tx=nullptr;else quiet=false;}
  if(quiet){for(auto pin:{GPIO_NUM_18,GPIO_NUM_17,GPIO_NUM_15,GPIO_NUM_21}){
   if(gpio_reset_pin(pin)!=ESP_OK)quiet=false;
   if(gpio_set_pull_mode(pin,GPIO_FLOATING)!=ESP_OK)quiet=false;
  }}
  if(!quiet)fail(ESP_CODEC_DEV_DRV_ERR);else opened=false;
  return quiet;
 }
};
static_assert(std::is_standard_layout<FixedI2s>::value && offsetof(FixedI2s,api)==0,"Data adapter layout");
}
