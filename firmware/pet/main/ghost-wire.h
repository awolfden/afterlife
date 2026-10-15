#pragma once
#include "wire.h"
#include <cstring>
namespace watcher_ghost {
struct Row {uint32_t boot=0,seq=0,index=0,state=0,unseen=0,pending=0,hue=0,rhythm=0,space_count=0;char title[33]{};uint8_t character=0;bool family=false;};
inline int hex(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;}
// Bounds, canonical UTF8, no NUL/control characters. Host additionally strips bidi.
inline bool text_valid(const char* text,size_t size){
 for(size_t i=0;i<size;){unsigned char c=text[i++];if(c<32||c==127)return false;if(c<128)continue;
  unsigned count;uint32_t cp,min;
  if(c>=0xc2&&c<=0xdf){count=1;cp=c&31;min=0x80;}else if(c>=0xe0&&c<=0xef){count=2;cp=c&15;min=0x800;}else if(c>=0xf0&&c<=0xf4){count=3;cp=c&7;min=0x10000;}else return false;
  if(count>size-i)return false;
  while(count--){unsigned char b=text[i++];if((b&0xc0)!=0x80)return false;cp=(cp<<6)|(b&63);}
  if(cp<min||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)||(cp>=0x80&&cp<=0x9f)||(cp>=0x2028&&cp<=0x202e)||(cp>=0x2066&&cp<=0x2069))return false;
 }return size>0;
}
inline bool parse(const char* line,size_t size,Row& result){
 if(size<4||size>=watcher_wire::max_line)return false;
 bool family=false;
#ifdef WATCHER_GHOST_FAMILY
 family=std::memcmp(line,"G2 ",3)==0;
#endif
 if(!family&&std::memcmp(line,"G1 ",3))return false;
 const char* p=line+3;const char* end=line+size;uint32_t v[10]{};
 for(unsigned i=0;i<(family?10u:9u);i++){if(!watcher_wire::number(p,end,v[i])||p==end||*p++!=' ')return false;}
 Row row;size_t used=0;
 while(p<end&&*p!=' '){if(end-p<2||used>=32)return false;const int a=hex(p[0]),b=hex(p[1]);if(a<0||b<0)return false;row.title[used++]=static_cast<char>((a<<4)|b);p+=2;}
 if(p==end||*p++!=' '||!text_valid(row.title,used))return false;
 const char* checksum=p;uint32_t crc;if(!watcher_wire::number(p,end,crc)||p!=end||watcher_wire::crc(line,checksum-line)!=crc)return false;
 if(!v[0]||!v[1]||v[2]>15||v[3]>7||v[4]>64||v[5]>64||v[4]>v[5]||v[6]>359||v[7]>3||!v[8]||v[8]>16||v[9]>5)return false;
 row.family=family;row.character=static_cast<uint8_t>(v[9]);
 row.boot=v[0];row.seq=v[1];row.index=v[2];row.state=v[3];row.unseen=v[4];row.pending=v[5];row.hue=v[6];row.rhythm=v[7];row.space_count=v[8];result=row;return true;
}
// Optional, display-only identity context. Does not renew W1 liveness or ACKs.
// Revisions are W1-sequence logical clocks, bound to an exact W1. A restarted
// host begins above the last accepted W1, without counter reuse in this boot.
struct FocusContext {
 uint32_t boot=0,seq=0,roster=0,selection=0;
 bool matches(const watcher_wire::Frame& frame)const{return boot==frame.boot&&seq==frame.seq&&roster&&selection;}
 bool accept(const char* line,size_t size,const watcher_wire::Frame& frame){
  if(size<4||size>=watcher_wire::max_line||std::memcmp(line,"K1 ",3))return false;
  const char* p=line+3;const char* end=line+size;uint32_t v[4];
  for(auto& value:v)if(!watcher_wire::number(p,end,value)||p==end||*p++!=' ')return false;
  const char* checksum=p;uint32_t crc;
  if(!watcher_wire::number(p,end,crc)||p!=end||watcher_wire::crc(line,checksum-line)!=crc)return false;
  if(!v[0]||!v[1]||!v[2]||!v[3]||v[2]>v[1]||v[3]>v[1]||v[0]!=frame.boot||v[1]!=frame.seq)return false;
  if(boot==v[0]&&(v[1]<=seq||v[2]<roster||v[3]<selection))return false;
  boot=v[0];seq=v[1];roster=v[2];selection=v[3];return true;
 }
};
enum class Mode { legacy, waiting, group, carousel };
class Page {
 // Union of a rolling +/-2 neighborhood and the old four-row page is <=6.
 // Legacy rows preserve compatibility with the previously installed firmware.
 Row rows[6]{};
 int64_t completed_at_=-1,ring_at_=-1;
 uint32_t completed_boot_=0,ring_boot_=0;
 static bool valid(const watcher_wire::Frame& f){return f.boot&&f.seq&&f.count&&f.count<=16&&f.selected<f.count;}
 static bool neighbor(unsigned index,const watcher_wire::Frame& f){
  const unsigned d=(index+f.count-f.selected)%f.count;
  return f.count<=5||d<=2||d>=f.count-2;
 }
 static bool admitted(unsigned index,const watcher_wire::Frame& f){return valid(f)&&index<f.count&&(index/4==f.selected/4||neighbor(index,f));}
 public:
 bool accept(const Row& row,const watcher_wire::Frame& frame,int64_t now=0){
  if(row.boot!=frame.boot||row.seq!=frame.seq||!admitted(row.index,frame)||get(row.index,frame))return false;
  Row* target=nullptr;
  for(auto& cached:rows)if(cached.boot!=frame.boot||cached.seq!=frame.seq){target=&cached;break;}
  if(!target)return false;
  *target=row;
  const bool ring=ring_complete(frame);
  if(complete(frame)||ring){completed_at_=now;completed_boot_=frame.boot;}
  if(ring){ring_at_=now;ring_boot_=frame.boot;}
  return true;
 }
 Mode mode(const watcher_wire::Frame& frame,int64_t now) const {
  // Partial traffic cannot extend this deadline or leave ACKs bound to old frames forever.
  if(!frame.count||completed_boot_!=frame.boot||completed_at_<0||now<completed_at_||now-completed_at_>1500)return Mode::legacy;
  if(ring_complete(frame))return Mode::carousel;
  if(ring_boot_==frame.boot&&ring_at_>=0&&now>=ring_at_&&now-ring_at_<=1500)return Mode::waiting;
  return complete(frame)?Mode::group:Mode::waiting;
 }
 bool complete(const watcher_wire::Frame& frame) const {
  if(!valid(frame))return false;
  const unsigned start=(frame.selected/4)*4;
  for(unsigned i=start;i<frame.count&&i<start+4;i++)if(!get(i,frame))return false;
  return frame.count>0;
 }
 bool ring_complete(const watcher_wire::Frame& frame)const{
  if(!valid(frame))return false;
  for(unsigned i=0;i<frame.count;i++)if(neighbor(i,frame)&&!get(i,frame))return false;
  return true;
 }
 const Row* get(unsigned index,const watcher_wire::Frame& frame) const {
  if(!admitted(index,frame))return nullptr;
  for(const auto& row:rows)if(row.boot==frame.boot&&row.seq==frame.seq&&row.index==index)return &row;
  return nullptr;
 }
};
}
