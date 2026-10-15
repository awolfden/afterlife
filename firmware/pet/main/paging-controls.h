#pragma once
#include <cstdint>
namespace watcher_touch {
// A button gesture that begins over preview geometry may still navigate or
// perform local safety controls, but must not ACK or enter/record voice.
// Retain the latch past the Buttons 25 ms release debounce.
class PreviewButtons {
 bool blocked_=false;int64_t released_=-1,last_=-1;
 public:
 bool update(unsigned value,int64_t now,bool ambiguous){
  if(now<0||(last_>=0&&now<last_)||value>3){blocked_=true;released_=-1;return true;}
  last_=now;
  if(value){released_=-1;if(ambiguous)blocked_=true;}
  const bool result=blocked_;
  if(!value){if(released_<0)released_=now;else if(now-released_>=30)blocked_=false;}
  return result;
 }
};
}
