#pragma once
#include "i2s_std.h"
constexpr int GPIO_FLOATING=0;
inline int gpio_reset_pin(int pin){
 events.push_back(6);assert(!clocked);
 if(fail_gpio)return -1;
 const unsigned bit=pin==18?1:pin==17?2:pin==15?4:8;routed&=~bit;return ESP_OK;
}
inline int gpio_set_pull_mode(int,int){return fail_gpio?-1:ESP_OK;}
