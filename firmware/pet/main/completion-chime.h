#pragma once
#include "wire.h"

namespace watcher_chime {
// Sound is a separate one-shot command, never inferred from selected-session state.
// A lost/duplicate/reconnected command cannot be replayed against a new frame.
class Gate {
 char command_;
 uint32_t boot_=0,last_=0;
 int64_t last_at_=-1000;
 public:
 explicit Gate(char command='C'):command_(command){}
 bool take(const char* line,size_t length,const watcher_wire::Frame& frame,int64_t now,bool linked){
  if(length<4 || length>=watcher_wire::max_line || line[0]!=command_ || line[1]!='1' || line[2]!=' ')return false;
  const char* p=line+3;const char* end=line+length;uint32_t boot,seq,sum;
  if(!watcher_wire::number(p,end,boot)||p==end||*p++!=' ')return false;
  if(!watcher_wire::number(p,end,seq)||p==end||*p++!=' ')return false;
  const char* checksum=p;
  if(!watcher_wire::number(p,end,sum)||p!=end||watcher_wire::crc(line,checksum-line)!=sum)return false;
  if(!linked||!boot||!seq||boot!=frame.boot||seq!=frame.seq)return false;
  if(boot_!=boot){boot_=boot;last_=0;}
  if(seq<=last_)return false;
  last_=seq; // Consume even muted/rate-limited commands; no deferred sounds.
  if((frame.flags&1)||!frame.count||now-last_at_<500)return false;
  last_at_=now;return true;
 }
};
}
