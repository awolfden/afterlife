#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>
inline bool rail=false,clocked=false,fail_alloc=false,fail_init=false,fail_delete=false,fail_gpio=false;
inline unsigned routed=0,allocations=0;
inline std::vector<int> events;
using i2s_chan_handle_t=void*;
constexpr int ESP_OK=0,I2S_NUM_0=0,I2S_ROLE_MASTER=0,I2S_DATA_BIT_WIDTH_16BIT=16,I2S_SLOT_MODE_STEREO=2;
constexpr int GPIO_NUM_18=18,GPIO_NUM_17=17,GPIO_NUM_15=15,GPIO_NUM_21=21,I2S_GPIO_UNUSED=-1;
struct i2s_chan_config_t{int dma_desc_num=0,dma_frame_num=0;bool auto_clear=false;};
struct i2s_std_config_t{int clk_cfg=0,slot_cfg=0;struct{int mclk=0,bclk=0,ws=0,dout=0,din=0;}gpio_cfg;};
#define I2S_CHANNEL_DEFAULT_CONFIG(a,b) i2s_chan_config_t{}
#define I2S_STD_CLK_DEFAULT_CONFIG(rate) rate
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bits,mode) bits
inline int i2s_new_channel(i2s_chan_config_t*,i2s_chan_handle_t* tx,void*){assert(rail);events.push_back(3);if(fail_alloc)return -1;*tx=reinterpret_cast<void*>(1);++allocations;return ESP_OK;}
inline int i2s_channel_init_std_mode(i2s_chan_handle_t,i2s_std_config_t*){assert(rail);clocked=true;routed=15;events.push_back(4);return fail_init?-1:ESP_OK;}
inline int i2s_del_channel(i2s_chan_handle_t){events.push_back(5);if(fail_delete)return -1;clocked=false;return ESP_OK;}
// Intentionally no TX-enable, PCM-write or codec API in the silent probe fixture.
