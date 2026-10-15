#pragma once
#include "esp_err.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>
using i2s_chan_handle_t=void*;
constexpr int I2S_NUM_0=0,I2S_ROLE_MASTER=1,I2S_MCLK_MULTIPLE_256=256,I2S_DATA_BIT_WIDTH_16BIT=16,I2S_SLOT_MODE_MONO=1,I2S_STD_SLOT_LEFT=1,I2S_GPIO_UNUSED=-1;
constexpr int GPIO_NUM_14=14,GPIO_NUM_18=18,GPIO_NUM_17=17,GPIO_NUM_15=15,GPIO_NUM_21=21;
struct i2s_chan_config_t {unsigned dma_desc_num=0,dma_frame_num=0;bool auto_clear=false;};
#define I2S_CHANNEL_DEFAULT_CONFIG(n,r) i2s_chan_config_t{}
struct i2s_clock {uint32_t rate=0;int mclk_multiple=256;};
#define I2S_STD_CLK_DEFAULT_CONFIG(rate) i2s_clock{rate,256}
struct i2s_slot {int bits=16,mode=1,slot_mask=1;};
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bits,mode) i2s_slot{bits,mode,1}
struct i2s_pins {int mclk,bclk,ws,dout,din;};
struct i2s_std_config_t {i2s_clock clk_cfg;i2s_slot slot_cfg;i2s_pins gpio_cfg;};
struct i2s_event_data_t {};
using Callback=bool(*)(i2s_chan_handle_t,i2s_event_data_t*,void*);
struct i2s_event_callbacks_t {Callback on_sent=nullptr;};
namespace factory_fake {
inline bool running=false,rail=false,pa=false,no_dma=false,short_write=false,write_timeout=false,fail_disable=false,fail_delete=false,fail_pins=false;
inline unsigned allocations=0,rx_allocations=0,reads=0,writes=0,nonzero=0;inline int64_t clock_value=0,pa_on=0;
inline Callback callback=nullptr;inline void* context=nullptr;inline void(*write_hook)()=nullptr;
inline unsigned fail_setup=0;inline bool tick_granularity=false;
inline int64_t tick_wait_budget(unsigned timeout,int64_t phase){return (timeout/10)*10-phase;}
inline std::vector<std::string> trace;
inline void progress(){if(running&&callback&&!no_dma)callback(nullptr,nullptr,context);}
inline void reset(){assert(!running);rail=pa=no_dma=short_write=write_timeout=fail_disable=fail_delete=fail_pins=false;allocations=rx_allocations=reads=writes=nonzero=0;clock_value=pa_on=0;callback=nullptr;context=nullptr;write_hook=nullptr;fail_setup=0;tick_granularity=false;trace.clear();}
}
inline int i2s_new_channel(const i2s_chan_config_t* cfg,i2s_chan_handle_t* tx,void* rx){using namespace factory_fake;if(rx)++rx_allocations;assert(!rx);assert(rail&&!running);assert(cfg->dma_desc_num==6&&cfg->dma_frame_num==256&&cfg->auto_clear);if(fail_setup==1)return ESP_FAIL;++allocations;*tx=reinterpret_cast<void*>(1);trace.push_back("allocate");return ESP_OK;}
inline int i2s_channel_read(i2s_chan_handle_t,void*,size_t,size_t* got,unsigned){++factory_fake::reads;assert(false&&"Output-only firmware must never read I2S hardware");*got=0;return ESP_FAIL;}
inline int i2s_channel_init_std_mode(i2s_chan_handle_t,const i2s_std_config_t* cfg){if(factory_fake::fail_setup==2)return ESP_FAIL;assert(cfg->clk_cfg.rate==44100&&cfg->clk_cfg.mclk_multiple==256&&cfg->slot_cfg.bits==16&&cfg->slot_cfg.mode==1&&cfg->slot_cfg.slot_mask==1);assert(cfg->gpio_cfg.mclk==18&&cfg->gpio_cfg.bclk==17&&cfg->gpio_cfg.ws==15&&cfg->gpio_cfg.dout==21&&cfg->gpio_cfg.din==-1);return ESP_OK;}
inline int i2s_channel_register_event_callback(i2s_chan_handle_t,const i2s_event_callbacks_t* cb,void* ctx){if(factory_fake::fail_setup==3)return ESP_FAIL;factory_fake::callback=cb->on_sent;factory_fake::context=ctx;return ESP_OK;}
inline int i2s_channel_preload_data(i2s_chan_handle_t,const void* data,size_t count,size_t* loaded){if(factory_fake::fail_setup==4)return ESP_FAIL;assert(count==3072);for(size_t i=0;i<count;++i)assert(static_cast<const uint8_t*>(data)[i]==0);*loaded=count;factory_fake::trace.push_back("zero-preload");return ESP_OK;}
inline int i2s_channel_enable(i2s_chan_handle_t){using namespace factory_fake;assert(rail&&!running&&!pa);if(fail_setup==5)return ESP_FAIL;running=true;trace.push_back("clocks-on");return ESP_OK;}
inline int i2s_channel_disable(i2s_chan_handle_t){using namespace factory_fake;assert(!pa);trace.push_back("clocks-off");if(fail_disable)return ESP_FAIL;running=false;return ESP_OK;}
inline int i2s_del_channel(i2s_chan_handle_t){using namespace factory_fake;assert(!running);trace.push_back("delete");return fail_delete?ESP_FAIL:ESP_OK;}
inline int i2s_channel_write(i2s_chan_handle_t,const void* data,size_t count,size_t* written,unsigned timeout){using namespace factory_fake;assert(running&&rail&&count<=512&&timeout>=36&&timeout<=60);++writes;if(tick_granularity&&writes>6&&tick_wait_budget(timeout,clock_value%10)<6){*written=0;return ESP_ERR_TIMEOUT;}const auto* pcm=static_cast<const int16_t*>(data);for(size_t i=0;i<count/2;++i){assert(pcm[i]>=-3276&&pcm[i]<=3276);if(pcm[i]){assert(pa&&clock_value-pa_on>=10);++nonzero;}}trace.push_back("pcm");clock_value+=6;progress();if(write_hook)write_hook();*written=short_write?count-2:count;return write_timeout?ESP_ERR_TIMEOUT:ESP_OK;}
