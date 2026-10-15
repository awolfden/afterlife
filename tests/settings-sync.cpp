#include "../firmware/pet/main/desk-settings.h"
#include <cassert>
#include <string>
#include <iostream>
using namespace watcher_settings;
static std::string echo(const std::string& token,unsigned revision=1,unsigned ack=0,unsigned flags=5,unsigned brightness=160,unsigned status=0,unsigned seq=10,unsigned forced=0){
 const auto prefix="S1 7 "+std::to_string(seq)+" "+token+" "+std::to_string(revision)+" "+std::to_string(ack)+" "+std::to_string(status)+" "+std::to_string(flags)+" "+std::to_string(brightness)+" "+std::to_string(forced)+" ";return prefix+std::to_string(watcher_wire::crc(prefix.data(),prefix.size()));
}
static bool take(Sync& s,const std::string& line){return s.receive(line.data(),line.size());}
int main(int argc,char**){
 const std::string one(32,'1'),two(32,'2');char out[193]{};
 if(argc>1){Sync s;s.context(7,10,0,true);std::string line;assert(std::getline(std::cin,line));assert(take(s,line));assert(s.change({7,240}));assert(s.edit(out));std::cout<<out;assert(s.advertisement(out));std::cout<<out;assert(s.interaction(out,1,3));std::cout<<out;return 0;}
 Sync s;assert(s.values()==Values{}&&s.save()==Save::temporary&&!s.supported());
 assert(s.change({7,240}));s.context(7,10,0,true);assert(!s.edit(out));assert(s.advertisement(out));
 assert(take(s,echo(one)));assert(s.values()==Values{}&&s.save()==Save::saved); // Offline edit never uploaded.
 assert(!take(s,echo(one,1,0,5,160,0,9)));assert(s.change({7,240}));assert(s.edit(out));assert(std::string(out).find(" 1 1 7 240 ")!=std::string::npos);
 assert(s.change({13,80}));s.context(7,10,249,true);assert(!s.edit(out));
 s.context(7,10,250,true);assert(s.edit(out));assert(std::string(out).find(" 1 1 7 240 ")!=std::string::npos);
 assert(take(s,echo(one,2,1,7,240)));assert(s.values()==(Values{13,80})&&s.save()==Save::saving);
 s.context(7,10,500,true);assert(s.edit(out));assert(std::string(out).find(" 2 2 13 80 ")!=std::string::npos);
 assert(!take(s,echo(one,2,1,7,240)));assert(take(s,echo(one,3,2,13,80)));assert(s.save()==Save::saved);
 assert(s.interaction(out,1,1));assert(std::string(out).rfind("S3 7 10 ",0)==0);
 assert(s.change({5,160}));s.context(7,10,750,true);assert(s.edit(out));
 assert(take(s,echo(one,3,3,13,80,1)));assert(s.save()==Save::unsaved&&s.values()==Values{});assert(!s.edit(out));
 s.context(7,10,800,false);assert(s.save()==Save::temporary);assert(s.change({15,240}));
 s.context(7,10,900,true);assert(take(s,echo(two,3,0,13,80)));assert(s.values()==(Values{13,80}));
 assert(!take(s,echo(one))); // A retired epoch must never restore over a newer one.
 Sync timeout;timeout.context(7,10,0,true);assert(take(timeout,echo(one)));timeout.change({7,160});assert(timeout.edit(out));
 timeout.context(7,10,1500,true);assert(timeout.save()==Save::unsaved&&!timeout.edit(out));assert(take(timeout,echo(one,2,1,7,160)));assert(timeout.save()==Save::saved);
 timeout.change({13,80});assert(timeout.edit(out));assert(std::string(out).find(" 2 2 13 80 ")!=std::string::npos);
 // Timeout never reuses an outstanding ID with a different payload.
 Sync retry;retry.context(7,10,0,true);assert(take(retry,echo(one)));retry.change({7,160});assert(retry.edit(out));retry.context(7,10,1500,true);
 assert(retry.change({13,80})&&retry.edit(out));assert(std::string(out).find(" 1 1 7 160 ")!=std::string::npos);
 assert(take(retry,echo(one,2,1,7,160)));assert(retry.save()==Save::saving&&retry.values()==(Values{13,80}));
 retry.context(7,10,1750,true);assert(retry.edit(out));assert(std::string(out).find(" 2 2 13 80 ")!=std::string::npos);
 Sync unavailable;unavailable.context(7,10,0,true);assert(take(unavailable,echo(one,8,0,7,240,1)));assert(unavailable.supported()&&unavailable.save()==Save::unsaved&&unavailable.values()==(Values{7,240}));unavailable.change({5,80});assert(!unavailable.edit(out));
 Sync lapse;lapse.context(7,10,0,true);assert(take(lapse,echo(one,3,2,5,160,0,10,1)));assert(lapse.forced()&&!lapse.animations());
 lapse.context(7,10,10,false);assert(!lapse.forced());lapse.context(7,11,20,true);assert(take(lapse,echo(one,3,2,5,160,0,11)));assert(lapse.save()==Save::saved);
 lapse.change({7,160});assert(lapse.edit(out));assert(std::string(out).find(" 3 3 7 160 ")!=std::string::npos);
 Sync churn;for(unsigned seq=1;seq<=200;++seq){char epoch[33];std::snprintf(epoch,sizeof epoch,"%032x",seq);churn.context(7,seq,seq*10,true);assert(take(churn,echo(epoch,1,0,5,160,0,seq)));}

 Echo parsed;assert(!parse("S1",2,parsed));assert(!parse((echo(one)+" ").c_str(),echo(one).size()+1,parsed));
 std::cout<<"watch settings epochs, optimistic edits, coalescing, retries and expiry pass\n";
}
