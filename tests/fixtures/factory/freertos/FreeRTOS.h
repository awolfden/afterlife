#pragma once
#include <cassert>
#define portTICK_PERIOD_MS 10
struct portMUX_TYPE {bool held;};
#define portMUX_INITIALIZER_UNLOCKED {false}
inline void portENTER_CRITICAL(portMUX_TYPE* p){assert(!p->held);p->held=true;}
inline void portEXIT_CRITICAL(portMUX_TYPE* p){assert(p->held);p->held=false;}
#define portENTER_CRITICAL_ISR portENTER_CRITICAL
#define portEXIT_CRITICAL_ISR portEXIT_CRITICAL
