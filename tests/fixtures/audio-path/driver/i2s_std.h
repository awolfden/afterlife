#pragma once
#include "../../audible/driver/i2s_std.h"
struct i2s_event_data_t {};
using i2s_isr_callback_t=bool(*)(i2s_chan_handle_t,i2s_event_data_t*,void*);
struct i2s_event_callbacks_t { i2s_isr_callback_t on_sent=nullptr; };
inline i2s_isr_callback_t tx_callback=nullptr;
inline void* tx_context=nullptr;
inline bool fail_callback=false;
inline int i2s_channel_register_event_callback(i2s_chan_handle_t,const i2s_event_callbacks_t* cb,void* user){if(fail_callback)return -1;tx_callback=cb->on_sent;tx_context=user;return ESP_OK;}
inline void dma_progress(unsigned count){assert(tx_running&&tx_callback);while(count--)assert(!tx_callback(nullptr,nullptr,tx_context));}
