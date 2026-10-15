// Immutable shared rows live in flash. No per-character RAM index or decoded canvas.
#pragma once
#include "../art/ghost-family.generated.h"
namespace watcher_render {
inline bool valid_family_art(){
 for(unsigned i=0;i<watcher_family::row_count;++i){
  const auto row=watcher_family::rows[i];
  if(!row.count||row.offset>watcher_family::run_count||row.count>watcher_family::run_count-row.offset)return false;
  unsigned x=0;
  for(unsigned j=0;j<row.count;++j){const auto run=watcher_family::runs[row.offset+j];const unsigned n=run.tag&0x7fff;if(!n||n>280-x)return false;x+=n;}
  if(x!=280)return false;
 }
 for(unsigned c=0;c<5;++c)for(unsigned s=0;s<9;++s)for(unsigned b=0;b<2;++b)for(unsigned y=0;y<280;++y)
  if(watcher_family::row_ids[c][s][b][y]>=watcher_family::row_count)return false;
 return true;
}
}
