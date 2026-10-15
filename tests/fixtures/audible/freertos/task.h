#pragma once
inline void vTaskDelay(int ticks){assert(rail&&!clocked&&ticks>=1);events.push_back(2);}
