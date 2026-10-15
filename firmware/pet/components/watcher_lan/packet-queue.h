#pragma once
#include "network.h"
namespace watcher_lan {
// Caller holds the network mailbox lock for every operation, including batches.
struct PacketQueue {
 Packet packets[16]{};unsigned head=0,count=0;
 void clear(){for(unsigned i=0;i<count;i++)wipe(&packets[(head+i)%16],sizeof(Packet));head=count=0;}
 bool push(const Packet& p){if(count==16)return false;packets[(head+count++)%16]=p;return true;}
 bool pop(Packet& p){if(!count)return false;p=packets[head];wipe(&packets[head],sizeof(Packet));head=(head+1)%16;count--;return true;}
 static unsigned layout(const char* bytes,size_t size,size_t (&ends)[4]){
  if(!bytes||!size||size>768)return 0;
  unsigned lines=0;size_t begin=0;
  for(size_t i=0;i<size;++i){
   if(bytes[i]=='\n'){if(i==begin||i+1-begin>192||lines==4)return 0;ends[lines++]=i+1;begin=i+1;}
   else if(static_cast<unsigned char>(bytes[i])<32||static_cast<unsigned char>(bytes[i])>126)return 0;
  }
  return begin==size?lines:0;
 }
 bool batch(uint32_t generation,int64_t at,const char* bytes,size_t size){
  size_t ends[4];const unsigned lines=layout(bytes,size,ends);
  if(!generation||!lines||lines>16-count)return false;
  size_t begin=0;Packet packet;packet.generation=generation;packet.at=at;
  for(unsigned i=0;i<lines;++i){packet.size=ends[i]-begin;std::memcpy(packet.bytes,bytes+begin,packet.size);push(packet);begin=ends[i];}
  wipe(&packet,sizeof packet);return true;
 }
};
} // namespace watcher_lan
