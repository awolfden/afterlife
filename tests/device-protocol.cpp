#include "../firmware/pet/main/wire.h"
#include "../firmware/pet/main/buttons.h"
#include <cassert>
#include <string>
#include <iostream>
#include <fstream>
using watcher_buttons::Action;
static unsigned checks=0;
static std::string wire(const std::string& fields){const std::string p="W1 "+fields+" ";return p+std::to_string(watcher_wire::crc(p.data(),p.size()));}
static bool parse(const std::string& line){watcher_wire::Frame f{};return watcher_wire::parse(line.data(),line.size(),f);}
static void rejected(const std::string& line){assert(!parse(line));++checks;}
static watcher_buttons::Buttons buttons(){watcher_buttons::Buttons b;b.update(0,0,7,10);b.update(0,25,7,10);return b;}
int main(int argc,char** argv){
 assert(watcher_wire::crc("123456789",9)==0xcbf43926u);
 const std::string valid=wire("99 1 0 3 0 0 1 0 1 0");watcher_wire::Frame f{};
 assert(watcher_wire::parse(valid.data(),valid.size(),f));assert(watcher_wire::alert_eligible(f));++checks;
 f.other=0;assert(!watcher_wire::alert_eligible(f));f.state=3;assert(watcher_wire::alert_eligible(f));f.flags=1;assert(!watcher_wire::alert_eligible(f));
 f.flags=0;f.state=7;assert(!watcher_wire::alert_eligible(f));f.other=1;assert(watcher_wire::alert_eligible(f));f.state=8;assert(!watcher_wire::alert_eligible(f));
 assert(watcher_wire::fresh(f,99,0));assert(!watcher_wire::fresh(f,100,0));assert(!watcher_wire::fresh(f,99,1));
 for(size_t i=0;i<valid.size();i++){std::string corrupt=valid;corrupt[i]=corrupt[i]=='0'?'1':'0';rejected(corrupt);}
 for(size_t n=0;n<valid.size();n++)rejected(valid.substr(0,n));
 for(const auto& fields:{"0 1 0 3 0 0 1 0 1 0","99 0 0 3 0 0 1 0 1 0","99 1 3 3 0 0 1 0 1 0","99 1 0 17 0 0 1 0 1 0","99 1 0 3 10 0 1 0 1 0","99 1 0 3 0 257 1 0 1 0","99 1 0 3 0 0 16 0 1 0","99 1 0 3 0 0 1 32 1 0","99 1 0 0 0 0 1 0 1 0","99 1 0 3 9 0 1 0 1 0","99 1 0 3 0 0 1 0 4294967296 0","99 -1 0 3 0 0 1 0 1 0"})rejected(wire(fields));
 rejected(valid+" ");rejected(std::string(192,'0'));assert(parse(wire("99 1 0 0 9 0 0 0 0 0")));
 {auto b=buttons();b.update(1,100,7,10);b.update(1,125,7,10);b.update(0,200,7,11);auto e=b.update(0,225,7,11);assert(e.action==Action::previous && e.frame==10 && e.boot==7);assert(b.update(0,300,7,11).action==Action::none);++checks;}
 {auto b=buttons();b.update(3,100,7,10);b.update(3,125,7,10);b.update(0,200,8,11);auto e=b.update(0,225,8,11);assert(e.action==Action::ack && e.boot==7);++checks;} // caller rejects old generation
 {auto b=buttons();b.update(1,100,7,10);b.update(1,125,7,10);b.update(3,500,7,11);b.update(3,525,7,11);assert(b.update(3,1724,7,12).action==Action::none);auto e=b.update(3,1725,7,12);assert(e.action==Action::mute && e.frame==11);assert(b.update(3,2000,7,12).action==Action::none);++checks;}
 {auto b=buttons();b.update(3,100,7,10);b.update(3,125,7,10);b.update(1,200,7,11);b.update(1,225,7,11);assert(b.update(1,2000,7,11).action==Action::none);b.update(0,2100,7,11);assert(b.update(0,2125,7,11).action==Action::ack);++checks;}
 {auto b=buttons();b.update(1,100,7,10);b.update(1,125,7,10);assert(b.update(1,4124,7,10).action==Action::none);assert(b.update(1,4125,7,10).action==Action::exit_to_init);++checks;}
 {auto b=buttons();b.update(2,100,7,10);b.update(2,125,7,10);assert(b.update(2,1924,7,10).action==Action::none);assert(b.update(2,1925,7,10).action==Action::brightness);++checks;}
 {watcher_buttons::Buttons b;b.update(2,0,7,10);assert(b.update(2,5000,7,10).action==Action::none);b.update(0,6000,7,10);assert(b.update(0,6025,7,10).action==Action::none);++checks;}
 {auto b=buttons();b.update(1,100,7,10);b.update(0,110,7,10);assert(b.update(0,200,7,10).action==Action::none);++checks;}
 {auto b=buttons();b.update(1,100,7,10);b.update(1,125,7,10);b.update(2,200,7,10);b.update(2,225,7,10);b.update(0,300,7,10);assert(b.update(0,325,7,10).action==Action::none);++checks;}
 {auto b=buttons();b.update(3,100,7,10);b.update(3,125,7,10);b.update(0,700,7,11);assert(b.update(0,725,7,11).action==Action::none);++checks;}
 {auto b=buttons();b.update(3,100,7,10);b.update(3,125,7,10);assert(b.update(3,1325,7,10).action==Action::mute);b.update(0,1400,7,10);assert(b.update(0,1425,7,10).action==Action::none);++checks;}
 {auto b=buttons();b.update(3,100,7,10);b.update(3,125,7,10);b.update(0,200,7,10);assert(b.update(0,225,7,10).action==Action::ack);++checks;}
 if(argc==2){std::ifstream input(argv[1]);assert(input.good());std::string line;unsigned n=0;while(std::getline(input,line)){assert(parse(line));++n;}assert(n>0);std::cout<<n<<" JavaScript-produced frames accepted by actual firmware parser\n";}
 std::cout<<checks<<" parser/button scenarios passed\n";
}
