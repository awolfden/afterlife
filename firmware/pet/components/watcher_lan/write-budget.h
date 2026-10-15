#pragma once
namespace watcher_lan {
enum class WriteStep {idle,blocked,progress,failed};
// Bound TLS write calls, not only completed packets. Yield immediately on WANT;
// the caller retains the exact pointer/length for the next owner iteration.
template<class Step> bool drain_writes(Step&& step){
 for(unsigned i=0;i<8;i++){
  switch(step()){
   case WriteStep::failed:return false;
   case WriteStep::idle:case WriteStep::blocked:return true;
   case WriteStep::progress:break;
  }
 }
 return true;
}
}
