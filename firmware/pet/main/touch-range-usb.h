#pragma once
#include "touch-range.h"
#include "touch-usb.h"
namespace watcher_touch {
inline size_t usb_range_line(char (&line)[192],uint32_t boot,uint32_t request,uint32_t seq,uint32_t rendered,const Sampler& sampler,const Swipe& swipe,bool eligible,unsigned blocked,const RangeSnapshot& r){
 size_t used=usb_diagnostic_prefix(line,boot,request,seq,rendered,sampler,swipe,eligible,blocked);
 if(!used)return 0;
 line[2]='2';
 const uint32_t values[]={r.id,r.state,r.samples,r.xMin,r.xMax,r.yMin,r.yMax,r.elapsed,r.windows};
 for(auto value:values){const int n=std::snprintf(line+used,sizeof(line)-used,"%lu ",static_cast<unsigned long>(value));if(n<=0||static_cast<size_t>(n)>=sizeof(line)-used)return 0;used+=n;}
 return usb_diagnostic_finish(line,used);
}
}
