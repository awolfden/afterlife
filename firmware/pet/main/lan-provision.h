#pragma once
#include "ghost-wire.h"
#include <cstdio>
#include <cstring>
namespace watcher_lan {
inline void wipe(void* data,size_t length){volatile unsigned char* p=static_cast<volatile unsigned char*>(data);while(length--)*p++=0;}
// Never pass by value or memcpy through an RTOS queue. The network owner's
// bounded mailbox must copy_from() under its lock and explicitly clear on revoke.
struct Credentials {
 char ssid[33]{},password[64]{},address[16]{},identity[65]{};uint8_t key[32]{};uint16_t port=0;
 Credentials()=default;Credentials(const Credentials&)=delete;Credentials& operator=(const Credentials&)=delete;
 ~Credentials(){clear();}
 void clear(){wipe(this,sizeof *this);}
 void copy_from(const Credentials& other){
  if(this==&other)return;
  std::memcpy(ssid,other.ssid,sizeof ssid);std::memcpy(password,other.password,sizeof password);
  std::memcpy(address,other.address,sizeof address);std::memcpy(identity,other.identity,sizeof identity);
  std::memcpy(key,other.key,sizeof key);port=other.port;
 }
};
inline bool nonzero(const uint8_t* p,size_t n){uint8_t value=0;while(n--)value|=*p++;return value!=0;}
inline bool decode(const uint8_t (&data)[168],Credentials& result){
 const unsigned ssid_size=data[1],password_size=data[2];
 if(data[0]!=1||!ssid_size||ssid_size>32||password_size<8||password_size>63)return false;
 if(!watcher_ghost::text_valid(reinterpret_cast<const char*>(data+3),ssid_size))return false;
 for(unsigned i=ssid_size;i<32;i++)if(data[3+i])return false;
 for(unsigned i=0;i<63;i++)if(i<password_size?(data[35+i]<32||data[35+i]>126):data[35+i]!=0)return false;
 const unsigned a=data[98],b=data[99];
 if(!(a==10||(a==172&&b>=16&&b<=31)||(a==192&&b==168)))return false;
 const unsigned port=(static_cast<unsigned>(data[102])<<8)|data[103];
 if(!port||!nonzero(data+104,32)||!nonzero(data+136,32))return false;
 Credentials candidate{};
 std::memcpy(candidate.ssid,data+3,ssid_size);std::memcpy(candidate.password,data+35,password_size);
 std::snprintf(candidate.address,sizeof candidate.address,"%u.%u.%u.%u",a,b,data[100],data[101]);candidate.port=port;
 std::memcpy(candidate.key,data+104,32);
 constexpr char hex[]="0123456789abcdef";
 for(unsigned i=0;i<32;i++){candidate.identity[2*i]=hex[data[136+i]>>4];candidate.identity[2*i+1]=hex[data[136+i]&15];}
 result.copy_from(candidate);return true;
}
enum class ProvisionResult { rejected, staged, complete };
// Only the prefix-gated physical USB dispatcher may call this assembler.
// Non-P1 input aborts assembly; a complete result requires take() in the SAME
// ingress step, not deferred to another task. Use ProvisionIngress below.
// It does no network, storage, logging or liveness refresh.
class Provision {
 uint8_t bytes_[168]{};uint32_t boot_=0,transaction_=0;unsigned next_=0;int64_t started_=0;bool ready_=false;
 public:
 Provision()=default;Provision(const Provision&)=delete;Provision& operator=(const Provision&)=delete;
 ~Provision(){clear();}
 void clear(){wipe(bytes_,sizeof bytes_);boot_=transaction_=0;next_=0;started_=0;ready_=false;}
 uint32_t transaction() const {return transaction_;}
 ProvisionResult receive(const char* line,size_t length,uint32_t boot,int64_t now){
  if(now<0||!boot||length<4||length>=watcher_wire::max_line||std::memcmp(line,"P1 ",3)){clear();return ProvisionResult::rejected;}
  const char* p=line+3;const char* end=line+length;uint32_t values[3];
  for(unsigned i=0;i<3;i++)if(!watcher_wire::number(p,end,values[i])||p==end||*p++!=' '){clear();return ProvisionResult::rejected;}
  const unsigned index=values[2],size=index==5?8:32;
  if(values[0]!=boot||!values[1]||index>5||end-p<static_cast<int>(size*2+2)){clear();return ProvisionResult::rejected;}
  uint8_t chunk[32]{};
  for(unsigned i=0;i<size;i++){const int a=watcher_ghost::hex(*p++),b=watcher_ghost::hex(*p++);if(a<0||b<0){wipe(chunk,sizeof chunk);clear();return ProvisionResult::rejected;}chunk[i]=static_cast<uint8_t>((a<<4)|b);}
  if(p==end||*p++!=' '){wipe(chunk,sizeof chunk);clear();return ProvisionResult::rejected;}
  const char* checksum=p;uint32_t crc;
  if(!watcher_wire::number(p,end,crc)||p!=end||watcher_wire::crc(line,checksum-line)!=crc){wipe(chunk,sizeof chunk);clear();return ProvisionResult::rejected;}
  if(index==0){clear();boot_=boot;transaction_=values[1];started_=now;}
  if(boot_!=boot||transaction_!=values[1]||index!=next_||now<started_||now-started_>5000||ready_){wipe(chunk,sizeof chunk);clear();return ProvisionResult::rejected;}
  std::memcpy(bytes_+index*32,chunk,size);wipe(chunk,sizeof chunk);next_++;
  if(next_<6)return ProvisionResult::staged;
  Credentials check{};ready_=decode(bytes_,check);wipe(&check,sizeof check);
  if(!ready_){clear();return ProvisionResult::rejected;}return ProvisionResult::complete;
 }
 bool take(Credentials& result,uint32_t boot,int64_t now){
  if(!ready_||boot!=boot_||now<started_||now-started_>5000){clear();return false;}
  const bool valid=decode(bytes_,result);clear();return valid;
 }
 void expire(uint32_t boot,int64_t now){if(boot!=boot_||now<started_||now-started_>5000)clear();}
};
enum class Source { usb, lan };
enum class IngressResult { ordinary, rejected, staged, delivered };
class ProvisionIngress {
 Provision provision_;
 public:
 void expire(uint32_t boot,int64_t now){provision_.expire(boot,now);}
 void clear(){provision_.clear();}
 // submit must synchronously copy into the owner's bounded mailbox or reject
 // without side effects. References must never escape this call. It must NOT
 // perform Wi-Fi/TLS work here. Origin comes from the transport, never the line.
 template<typename Submit>
 IngressResult dispatch(Source source,const char* line,size_t length,uint32_t boot,int64_t now,Submit&& submit){
  if(length<3||std::memcmp(line,"P1 ",3))return IngressResult::ordinary;
  if(source!=Source::usb)return IngressResult::rejected;
  const auto state=provision_.receive(line,length,boot,now);
  if(state==ProvisionResult::rejected)return IngressResult::rejected;
  if(state==ProvisionResult::staged)return IngressResult::staged;
  Credentials credentials;const uint32_t transaction=provision_.transaction();
  if(!provision_.take(credentials,boot,now))return IngressResult::rejected;
  return submit(static_cast<const Credentials&>(credentials),transaction)?IngressResult::delivered:IngressResult::rejected;
 }
};
}
