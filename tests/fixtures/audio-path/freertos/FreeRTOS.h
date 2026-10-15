#pragma once
#include "../../power-first/freertos/FreeRTOS.h"
struct portMUX_TYPE {bool held;};
#define portMUX_INITIALIZER_UNLOCKED {false}
inline void portENTER_CRITICAL(portMUX_TYPE* mux){assert(!mux->held);mux->held=true;}
inline void portEXIT_CRITICAL(portMUX_TYPE* mux){assert(mux->held);mux->held=false;}
#define portENTER_CRITICAL_ISR portENTER_CRITICAL
#define portEXIT_CRITICAL_ISR portEXIT_CRITICAL
