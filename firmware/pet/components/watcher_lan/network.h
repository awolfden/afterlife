#pragma once
#include "lan-provision.h"
namespace watcher_lan {
struct Packet {uint32_t generation=0;int64_t at=0;size_t size=0;char bytes[192]{};};
// Single network-task ownership. All public calls are bounded mailbox operations;
// none performs Wi-Fi, DNS, TLS or I2C. Object must outlive its owner task.
class Network {
 struct Impl;Impl* impl_=nullptr;
 public:
 Network()=default;Network(const Network&)=delete;Network& operator=(const Network&)=delete;
 bool start();
 bool submit(const Credentials& credentials);
 uint32_t connection() const;
 void drop(uint32_t generation);
 bool send(uint32_t generation,const char* bytes,size_t size);
 bool send_batch(uint32_t generation,const char* bytes,size_t size); // Atomic admission: <=4 complete <=192-byte lines.
 bool receive(Packet& packet);
 bool stop(); // Request cleanup; returns true only after acknowledged teardown.
 bool fault() const;
 bool has_ip() const;
};
}
