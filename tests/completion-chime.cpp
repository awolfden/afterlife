#include <cassert>
#include <string>
#include "../firmware/pet/main/completion-chime.h"
static std::string command(unsigned boot,unsigned seq){const auto p="C1 "+std::to_string(boot)+" "+std::to_string(seq)+" ";return p+std::to_string(watcher_wire::crc(p.data(),p.size()));}
int main(){
 watcher_chime::Gate gate;watcher_wire::Frame f{};f.boot=99;f.seq=1;f.count=1;
 auto take=[&](std::string s,int64_t at,bool linked=true){return gate.take(s.data(),s.size(),f,at,linked);};
 assert(!take(command(98,1),0));assert(!take(command(99,2),0));assert(!take(command(99,1)+"x",0));
 assert(!take(command(99,1),0,false));assert(take(command(99,1),0));assert(!take(command(99,1),1000));
 f.seq=2;f.flags=1;assert(!take(command(99,2),1000));f.flags=0;assert(!take(command(99,2),2000));
 f.seq=3;assert(take(command(99,3),2000));f.seq=4;assert(!take(command(99,4),2100));assert(!take(command(99,4),3000));
 f.boot=100;f.seq=1;assert(!take(command(99,4),3000));assert(take(command(100,1),3000));
 f.seq=2;f.count=0;assert(!take(command(100,2),4000));
 for(unsigned i=0;i<10000;i++){const auto bad=std::string(i%192,'x');assert(!take(bad,5000));}
}
