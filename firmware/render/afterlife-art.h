// Static disconnected branding. Immutable row lookup; no extra canvas or index RAM.
#pragma once
#include <algorithm>
#include "../art/afterlife.generated.h"
namespace watcher_render {
inline bool valid_afterlife_art(){
 using namespace watcher_afterlife;
 if(offsets[0]!=0||offsets[height]!=run_count)return false;
 for(unsigned y=0;y<height;++y){
  if(offsets[y]>=offsets[y+1]||offsets[y+1]>run_count)return false;
  unsigned x=0;
  for(unsigned i=offsets[y];i<offsets[y+1];++i){const unsigned n=runs[i].tag;if(!n||n>width-x)return false;x+=n;}
  if(x!=width)return false;
 }
 return true;
}
template<class Surface> void draw_afterlife(int left,int top,int right,int bottom,Surface& surface,uint32_t& visited){
 using namespace watcher_afterlife;
 for(int y=top;y<bottom;++y){int x=0;for(unsigned i=offsets[y];i<offsets[y+1];++i){const auto& run=runs[i];const int end=x+run.tag;++visited;const int a=std::max(left,x),b=std::min(right,end);if(b>a)surface.span(a,y,b-a,run.color);x=end;}}
}
}
