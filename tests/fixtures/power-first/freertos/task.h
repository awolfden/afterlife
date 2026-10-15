#pragma once
inline void vTaskDelay(int ticks){assert(rail&&!clocked&&ticks>=2);events.push_back(2);}
