#pragma once
#include <cstddef>
#include <cstdint>
namespace watcher_settings {
// Before W1 selects a transport, publish discovery to both candidates. In that
// state the caller supplies D2 only (no live settings context). Once selected,
// only the active transport may admit the transactional settings batch.
template<class Source,class Usb,class Lan>
bool status_route(Source source,uint32_t generation,uint32_t connected,const char* bytes,size_t size,Usb usb,Lan lan){
 if(source==Source::lan)return generation&&lan(generation,bytes,size);
 const bool serial=usb(bytes,size);
 if(source==Source::usb)return serial;
 const bool network=connected&&lan(connected,bytes,size); // Never short-circuit discovery on USB success.
 return serial||network;
}
} // namespace watcher_settings
