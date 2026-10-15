#pragma once
#include "i2s_std.h"
constexpr int GPIO_FLOATING=0,GPIO_MODE_OUTPUT=1;
inline int gpio_set_level(int pin,int value){assert(pin==14);factory_fake::trace.push_back(value?"gpio-pa-on":"gpio-pa-off");return ESP_OK;}
inline int gpio_set_direction(int,int){return ESP_OK;}
inline int gpio_reset_pin(int){assert(!factory_fake::running);factory_fake::trace.push_back("float");return factory_fake::fail_pins?ESP_FAIL:ESP_OK;}
inline int gpio_set_pull_mode(int,int){return factory_fake::fail_pins?ESP_FAIL:ESP_OK;}
