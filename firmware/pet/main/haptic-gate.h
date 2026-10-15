#pragma once
#include "wire.h"
namespace watcher_feedback {
// Main-loop owned. Consume before dispatch, including muted/ineligible/busy events.
// Transport reconnect safety additionally relies on the host's durable claim ledger.
class HapticGate {
 uint32_t boot_=0,seen_=0;
 public:
 bool take(const watcher_wire::Frame& frame){
  if(!frame.boot||!frame.seq)return false;
  if(frame.boot!=boot_){boot_=frame.boot;seen_=0;}
  if(!frame.alert||frame.alert<=seen_)return false;
  seen_=frame.alert;
  return watcher_wire::alert_eligible(frame);
 }
};
}
