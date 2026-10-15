#pragma once
// Host-only minimal test double; never included in the firmware build.
#include <cstddef>
#include <cstdint>
#include <vector>
using i2s_chan_handle_t=void*;
constexpr int ESP_OK=0,I2S_NUM_0=0,I2S_ROLE_MASTER=0,I2S_DATA_BIT_WIDTH_16BIT=16,I2S_SLOT_MODE_STEREO=2;
constexpr int GPIO_NUM_18=18,GPIO_NUM_17=17,GPIO_NUM_15=15,GPIO_NUM_21=21,I2S_GPIO_UNUSED=-1;
struct i2s_chan_config_t{int dma_desc_num=0,dma_frame_num=0;bool auto_clear=false;};
struct i2s_std_config_t{int clk_cfg=0,slot_cfg=0;struct{int mclk=0,bclk=0,ws=0,dout=0,din=0;}gpio_cfg;};
#define I2S_CHANNEL_DEFAULT_CONFIG(a,b) i2s_chan_config_t{}
#define I2S_STD_CLK_DEFAULT_CONFIG(rate) rate
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bits,mode) bits
#define pdMS_TO_TICKS(n) n
inline void vTaskDelay(int){}
inline bool fail_write=false,enabled=false;
inline std::vector<int16_t> samples;
inline int i2s_new_channel(i2s_chan_config_t*,i2s_chan_handle_t* tx,void*){*tx=reinterpret_cast<void*>(1);return ESP_OK;}
inline int i2s_channel_init_std_mode(i2s_chan_handle_t,i2s_std_config_t*){return ESP_OK;}
inline int i2s_del_channel(i2s_chan_handle_t){return ESP_OK;}
inline int i2s_channel_enable(i2s_chan_handle_t){enabled=true;return ESP_OK;}
inline int i2s_channel_disable(i2s_chan_handle_t){enabled=false;return ESP_OK;}
inline int i2s_channel_write(i2s_chan_handle_t,const void* src,size_t size,size_t* written,unsigned){
 if(fail_write){*written=0;return -1;}
 const auto* pcm=static_cast<const int16_t*>(src);samples.insert(samples.end(),pcm,pcm+size/2);*written=size;return ESP_OK;
}
