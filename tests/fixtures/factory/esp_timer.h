#pragma once
#include "driver/i2s_std.h"
inline int64_t esp_timer_get_time(){return factory_fake::clock_value*1000;}
