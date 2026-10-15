#pragma once
#include "FreeRTOS.h"
#include "driver/i2s_std.h"
using TaskHandle_t=void*;
#define pdMS_TO_TICKS(ms) ((ms)/10)
constexpr int pdPASS=1,eSetBits=1;
namespace factory_fake {inline unsigned tasks_created=0;inline bool task_fail=false;inline void(*delay_hook)()=nullptr;}
inline int xTaskCreate(void(*)(void*),const char*,unsigned,void*,unsigned,TaskHandle_t* task){++factory_fake::tasks_created;if(factory_fake::task_fail)return 0;*task=reinterpret_cast<void*>(1);return pdPASS;}
inline void xTaskNotify(TaskHandle_t,uint32_t,int){}
inline void xTaskNotifyWait(uint32_t,uint32_t,uint32_t*,unsigned){}
inline void vTaskDelay(unsigned ticks){factory_fake::clock_value+=ticks*10;factory_fake::progress();if(factory_fake::delay_hook)factory_fake::delay_hook();}
