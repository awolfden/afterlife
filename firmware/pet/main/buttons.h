#pragma once
#include <stdint.h>
namespace watcher_buttons {
enum class Action:uint8_t {none,previous,next,ack,mute,exit_to_init,brightness};
struct Event {Action action=Action::none;uint32_t boot=0,frame=0;};
class Buttons {
  int raw_=-1,stable_=-1;uint64_t change_=0,pressed_=0,last_=0;
  unsigned gesture_=0;uint32_t boot_=0,frame_=0;bool ready_=false,handled_=false;
public:
  Event update(unsigned value,uint64_t now,uint32_t boot,uint32_t shown) {
    if(value>3 || now<last_)return {};
    last_=now;
    if(static_cast<int>(value)!=raw_){raw_=value;change_=now;}
    if(now-change_<25)return {};
    if(!ready_){if(!value){ready_=true;stable_=0;}return {};}
    Event result{};
    if(static_cast<int>(value)!=stable_) {
      if(stable_==0 && value){pressed_=now;gesture_=value;handled_=false;boot_=boot;frame_=shown;}
      if(value==3 && gesture_!=3){gesture_=3;pressed_=now;boot_=boot;frame_=shown;}
      if(stable_ && value && value!=3 && gesture_!=3){handled_=true;gesture_=0;} // ambiguous A-to-B rollover
      if(!value && !handled_){
        // Retired mode-entry chord remains inert; never turn it into an ACK.
        const bool retired=gesture_==3&&now-pressed_>=600&&now-pressed_<1200;
        result={retired?Action::none:gesture_==3?Action::ack:gesture_==1?Action::previous:gesture_==2?Action::next:Action::none,boot_,frame_};
      }
      stable_=value;
    }
    if(!value || handled_)return result;
    Action action=Action::none;
    if(gesture_==3 && value==3 && now-pressed_>=1200)action=Action::mute;
    else if(gesture_==1 && now-pressed_>=4000)action=Action::exit_to_init;
    else if(gesture_==2 && now-pressed_>=1800)action=Action::brightness;
    if(action!=Action::none){handled_=true;return {action,boot_,frame_};}
    return result;
  }
};
} // namespace watcher_buttons
