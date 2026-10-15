#pragma once
#include "touch.h"
#include <cstdio>
namespace watcher_touch {
// Read-only, local display only. No bus, USB, logs, coordinates or session data.
inline unsigned display_count(uint16_t value){return value>999?999:value;}
inline void diagnostic_lines(char (&top)[48],char (&bottom)[48],const Sampler& sampler,const Swipe& swipe,bool eligible,unsigned blocked=0){
 const auto& s=sampler.stats();
 std::snprintf(top,sizeof(top),"T%u F%u I%u B%u D%u N%u H%u V%u",sampler.halted()?9u:sampler.stage(),display_count(s.setupFailures),display_count(s.ioErrors),display_count(s.invalid),display_count(s.gapMs),display_count(s.invalidCount),display_count(s.invalidX),display_count(s.invalidY));
 std::snprintf(bottom,sizeof(bottom),"C%u U%u A%u X%u G%u S%u Z%02x",display_count(s.contacts),display_count(s.releases),swipe.armed()?1u:0u,swipe.tracking()?1u:0u,eligible?1u:0u,display_count(swipe.emitted()),blocked&255u);
}
} // namespace watcher_touch
