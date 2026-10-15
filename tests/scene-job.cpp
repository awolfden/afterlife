#include "../firmware/render/scene-job.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <stdexcept>
#include <vector>
#define CHECK(expression) do {if(!(expression)){std::cerr<<__LINE__<<": " #expression "\n";std::abort();}} while(false)
using namespace watcher_render;
struct Surface {
 std::vector<uint16_t> pixels=std::vector<uint16_t>(466*466,0);
 std::vector<std::string> calls;unsigned cleared=0,max_clear=0;bool throw_span=false;
 void clear_rows(unsigned y,unsigned rows,uint16_t color){CHECK(y==cleared&&y+rows<=466);cleared+=rows;max_clear=std::max(max_clear,rows);std::fill(pixels.begin()+y*466,pixels.begin()+(y+rows)*466,color);}
 void span(int x,int y,int n,uint16_t color){if(throw_span)throw std::runtime_error("span");CHECK(x>=0&&y>=0&&n>0&&x+n<=466&&y<466);std::fill(pixels.begin()+y*466+x,pixels.begin()+y*466+x+n,color);calls.push_back("span:"+std::to_string(x)+":"+std::to_string(y)+":"+std::to_string(n)+":"+std::to_string(color));}
 void shadow(){calls.push_back("shadow");}
 void circle(int x,int y,int r,uint16_t c,bool filled){calls.push_back("circle:"+std::to_string(x)+":"+std::to_string(y)+":"+std::to_string(r)+":"+std::to_string(c)+":"+std::to_string(filled));}
 void text(const char* value,int y,float size,uint16_t color){calls.push_back("text:"+std::string(value)+":"+std::to_string(y)+":"+std::to_string(size)+":"+std::to_string(color));}
};
int main(){
 unsigned cases=0;
 for(unsigned state=0;state<9;++state)for(unsigned budget:{1u,7u,64u}){
  SceneProgram program;Surface expected,actual;expected.clear_rows(0,466,0x0842);
  CHECK(program.shadow());expected.shadow();
  for(unsigned layer=0;layer<4;++layer){
   const bool personality=layer!=0,blink=(state&1)!=0;const int x=120+layer*65,y=180+layer*10;
   const unsigned scale=64+layer*64,hue=layer*90,alpha=layer?128:255;
   CHECK(program.ghost(static_cast<State>(state),blink,x,y,personality,scale,hue,layer,alpha));
   const auto sink=[&](int xx,int yy,int n,uint16_t c){expected.span(xx,yy,n,carousel_color(c,0x0842,alpha));};
   if(personality)CHECK(draw_personality(static_cast<State>(state),blink,x,y,scale,hue,layer,sink));
   else CHECK(draw_ghost(static_cast<State>(state),blink,x,y,sink));
   if(layer){CHECK(program.circle(x+12,y-12,6,0xf672,false));expected.circle(x+12,y-12,6,0xf672,false);CHECK(program.circle(x+12,y-12,3,0xf672,true));expected.circle(x+12,y-12,3,0xf672,true);}
  }
  char title[]="Session 1";CHECK(program.text(title,53,2.5f,0xef7d));expected.text(title,53,2.5f,0xef7d);
  for(int i=0;i<16;++i){CHECK(program.circle(233+(i-7)*12,91,i==0?4:2,0xb43a,true));expected.circle(233+(i-7)*12,91,i==0?4:2,0xb43a,true);}
  for(int y:{340,381,410,427,443}){CHECK(program.text("Owned footer",y,1.3f,0xbdd7));expected.text("Owned footer",y,1.3f,0xbdd7);}
  SceneJob job;CHECK(job.begin(program));CHECK(!job.begin(program));CHECK(job.step(0,actual).work==0&&actual.cleared==0);
  title[0]='X';program=SceneProgram{}; // Neither caller strings nor builder remain borrowed.
  unsigned ticks=0;
  while(!job.complete()){CHECK(++ticks<1000000);const auto progress=job.step(budget,actual);CHECK(progress.work<=budget&&progress.work>0&&progress.phase!=ScenePhase::failed);}
  CHECK(actual.cleared==466&&actual.max_clear<=8);CHECK(actual.pixels==expected.pixels);CHECK(actual.calls==expected.calls);CHECK(job.step(64,actual).work==0);++cases;
 }
 // Capacity/parameter errors reject before output; cancellation permits fresh RAM work.
 SceneProgram empty;SceneJob bad;CHECK(!bad.begin(empty));Surface untouched;CHECK(bad.step(100,untouched).work==0&&untouched.cleared==0);
 SceneProgram too_many;for(unsigned i=0;i<40;++i)CHECK(too_many.shadow());CHECK(!too_many.shadow()&&!bad.begin(too_many));
 SceneProgram too_long;const std::string long_text(64,'x');CHECK(!too_long.text(long_text.c_str(),53,1,0)&&!bad.begin(too_long));
 SceneProgram too_many_texts;for(unsigned i=0;i<8;++i)CHECK(too_many_texts.text("x",53,1,0));CHECK(!too_many_texts.text("x",53,1,0));
 SceneProgram invalid;CHECK(!invalid.ghost(State::idle,false,0,0,true,257)&&!bad.begin(invalid));
 SceneProgram valid;CHECK(valid.shadow());CHECK(bad.begin(valid));CHECK(bad.step(1,untouched).work==1);bad.cancel();CHECK(bad.step(100,untouched).work==0);Surface restart;CHECK(bad.begin(valid));while(!bad.complete())bad.step(1,restart);CHECK(restart.cleared==466);
 SceneProgram one;CHECK(one.ghost(State::idle,false,233,215));SceneJob exception;Surface throws;throws.throw_span=true;CHECK(exception.begin(one));bool caught=false;
 try{while(!exception.complete())exception.step(64,throws);}catch(const std::runtime_error&){caught=true;}
 CHECK(caught&&exception.phase()==ScenePhase::failed&&exception.step(64,throws).work==0);Surface retry;CHECK(exception.begin(one));while(!exception.complete())exception.step(64,retry);
 std::cout<<"scene job: "<<cases<<" owned ordered command/span cases; bounded clear/work, capacity and cancellation pass\n";
}
