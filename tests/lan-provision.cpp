#include "../firmware/pet/main/lan-provision.h"
#include <cassert>
#include <iostream>
#include <string>
#include <type_traits>
static_assert(!std::is_copy_constructible<watcher_lan::Credentials>::value);
static_assert(!std::is_copy_assignable<watcher_lan::Credentials>::value);
int main(int argc,char** argv){
 assert(argc==2);const std::string mode=argv[1];watcher_lan::Provision provision;std::string line;unsigned count=0;
 if(mode=="ingress"||mode=="busy"){
  watcher_lan::ProvisionIngress ingress;watcher_lan::Credentials mailbox;unsigned calls=0;
  const auto submit=[&](const watcher_lan::Credentials& credentials,uint32_t transaction){assert(transaction==99);calls++;if(mode=="busy")return false;mailbox.copy_from(credentials);return true;};
  while(std::getline(std::cin,line)){
   assert(ingress.dispatch(watcher_lan::Source::usb,"Q1",2,42,count*10,submit)==watcher_lan::IngressResult::ordinary);
   assert(ingress.dispatch(watcher_lan::Source::lan,line.data(),line.size(),42,count*10,submit)==watcher_lan::IngressResult::rejected);
   const auto state=ingress.dispatch(watcher_lan::Source::usb,line.data(),line.size(),42,count*10,submit);
   const auto expected=count<5?watcher_lan::IngressResult::staged:mode=="busy"?watcher_lan::IngressResult::rejected:watcher_lan::IngressResult::delivered;
   assert(state==expected);assert(calls==(count<5?0u:1u));count++;
  }
  assert(count==6);assert(calls==1);
  if(mode=="ingress"){assert(mailbox.port==4444);for(auto b:mailbox.key)assert(b==0x22);}
  else {assert(mailbox.port==0);for(auto b:mailbox.key)assert(b==0);}
  mailbox.clear();std::cout<<"provision-ok\n";return 0;
 }
 while(std::getline(std::cin,line)){
  const auto state=provision.receive(line.data(),line.size(),mode=="max"?0xffffffff:mode=="boot"?43:42,mode=="negative"?-1:mode=="backwards"?100-count:mode=="slow"?count*1001:count*10);
  if(mode=="valid"||mode=="max")assert(state==(count==5?watcher_lan::ProvisionResult::complete:watcher_lan::ProvisionResult::staged));
  count++;
 }
 assert(count);watcher_lan::Credentials result{};
 if(mode=="clear")provision.clear();
 if(mode=="expire")provision.expire(42,6000);
 const bool valid=provision.take(result,mode=="max"?0xffffffff:mode=="take-boot"?43:42,(mode=="slow"||mode=="take-expired")?6000:100);
 if(mode=="valid"||mode=="restart"||mode=="max"){
  assert(valid);
  assert(std::string(result.ssid)==(mode=="max"?std::string(32,'x'):"café"));
  assert(std::string(result.password)==(mode=="max"?std::string(63,'y'):"test password"));
  assert(!std::strcmp(result.address,"192.168.1.2"));assert(result.port==(mode=="max"?65535:4444));
  assert(!std::strcmp(result.identity,"1111111111111111111111111111111111111111111111111111111111111111"));
  for(auto b:result.key)assert(b==0x22);
  assert(!provision.take(result,42,100));
 }else assert(!valid);
 watcher_lan::wipe(&result,sizeof result);for(size_t i=0;i<sizeof result;i++)assert(reinterpret_cast<unsigned char*>(&result)[i]==0);
 std::cout<<"provision-ok\n";
}
