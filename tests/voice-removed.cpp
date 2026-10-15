#include <cassert>
#include <cstdio>
#include "../firmware/pet/main/factory-codec.h"
#include "../firmware/pet/main/buttons.h"
#include "../firmware/pet/components/watcher_lan/write-budget.h"
int main(){
 using namespace factory_fake;
 reset();rail=true;
 watcher_feedback::FixedI2s data;
 assert(data.prepare());assert(allocations==1&&rx_allocations==0&&reads==0);
 uint8_t silence[512]{};
 assert(data.api.write(&data.api,silence,sizeof silence)==ESP_CODEC_DEV_OK);
 assert(writes==1&&rx_allocations==0&&reads==0);
 assert(data.api.read(&data.api,silence,sizeof silence)==ESP_CODEC_DEV_NOT_SUPPORT);
 assert(data.error==ESP_CODEC_DEV_NOT_SUPPORT&&reads==0);
 assert(data.release());assert(!running&&data.tx==nullptr);rail=false;
 // Preserve shared TLS WANT/idle/failure and eight-attempt bounds from the retired packetizer suite.
 for(const auto stop:{watcher_lan::WriteStep::idle,watcher_lan::WriteStep::blocked,watcher_lan::WriteStep::failed}){
  unsigned calls=0;const bool ok=watcher_lan::drain_writes([&](){++calls;return stop;});assert(calls==1);assert(ok==(stop!=watcher_lan::WriteStep::failed));
 }
 unsigned calls=0;assert(watcher_lan::drain_writes([&](){++calls;return watcher_lan::WriteStep::progress;}));assert(calls==8);
 for(unsigned stop=1;stop<=8;stop++){
  calls=0;assert(watcher_lan::drain_writes([&](){return ++calls==stop?watcher_lan::WriteStep::blocked:watcher_lan::WriteStep::progress;}));assert(calls==stop);
 }
 // Stable press starts at 55ms, stable release at 55+held. No retired chord becomes ACK.
 for(unsigned held:{599u,600u,900u,1199u}){
  watcher_buttons::Buttons buttons;buttons.update(0,0,1,2);buttons.update(0,25,1,2);
  buttons.update(3,30,1,2);buttons.update(3,55,1,2);
  buttons.update(0,30+held,1,2);auto event=buttons.update(0,55+held,1,2);
  assert(event.action==(held<600?watcher_buttons::Action::ack:watcher_buttons::Action::none));
 }
 watcher_buttons::Buttons buttons;buttons.update(0,0,1,2);buttons.update(0,25,1,2);buttons.update(3,30,1,2);buttons.update(3,55,1,2);
 assert(buttons.update(3,1255,1,2).action==watcher_buttons::Action::mute);
 std::puts("output-only, retired gesture and LAN budget checks passed");
}
