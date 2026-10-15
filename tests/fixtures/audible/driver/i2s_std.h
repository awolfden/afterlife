#pragma once
#include "../../power-first/driver/i2s_std.h"
inline bool tx_running=false,preloaded=false,fail_preload=false,short_preload=false,fail_enable=false,fail_disable=false;
inline std::vector<int16_t> pcm_samples;
inline int i2s_channel_preload_data(i2s_chan_handle_t,const void* data,size_t bytes,size_t* loaded){
 assert(rail&&clocked&&!tx_running);events.push_back(8);
 if(fail_preload){*loaded=0;return -1;}
 const auto* p=static_cast<const int16_t*>(data);pcm_samples.assign(p,p+bytes/2);
 *loaded=short_preload?bytes/2:bytes;preloaded=true;return ESP_OK;
}
inline int i2s_channel_enable(i2s_chan_handle_t){assert(rail&&clocked&&preloaded);events.push_back(9);if(fail_enable)return -1;tx_running=true;return ESP_OK;}
inline int i2s_channel_disable(i2s_chan_handle_t){events.push_back(10);if(fail_disable)return -1;tx_running=false;return ESP_OK;}
