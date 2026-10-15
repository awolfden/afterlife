#include "../firmware/render/raster-job.h"
#include "../firmware/render/carousel.h"
#include <cassert>
#include <iostream>
#include <vector>
using namespace watcher_render;
struct Span {int x,y,n;uint16_t color;bool operator==(const Span& b)const{return x==b.x&&y==b.y&&n==b.n&&color==b.color;}};
struct Image {
 std::vector<uint16_t> pixels=std::vector<uint16_t>(logical_width*logical_height,0x0842);
 std::vector<Span> spans;
 void put(int x,int y,int n,uint16_t color){assert(x>=0&&y>=0&&n>0&&x+n<=static_cast<int>(logical_width)&&y<static_cast<int>(logical_height));spans.push_back({x,y,n,color});for(int i=0;i<n;++i)pixels[y*logical_width+x+i]=color;}
};
static size_t drain(RasterJob& job,Image& image,unsigned budget,uint8_t alpha=255){
 size_t total=0;unsigned steps=0;
 while(job.active()){
  const auto result=job.step(budget,[&](int x,int y,int n,uint16_t c){image.put(x,y,n,carousel_color(c,0x0842,alpha));});
  assert(result.work>0&&result.work<=budget&&result.spans<=result.work);
  total+=result.work;assert(++steps<=watcher_art::width*watcher_art::height*2);
 }
 return total;
}
static void malformed(const watcher_art::Sprite& sprite){
 RasterJob job;Image image;job.begin_sprite(sprite,233,215);
 drain(job,image,1);assert(job.phase()==RasterPhase::failed);assert(image.spans.empty());
 assert(job.step(500,[](int,int,int,uint16_t){assert(false);}).work==0);
}
int main(){
 unsigned cases=0;
 for(unsigned state=0;state<9;++state)for(bool blink:{false,true})for(int center:{-140,0,233,466,600}){
  Image expected,actual;const auto art=static_cast<State>(state);
  assert(draw_ghost(art,blink,center,center,[&](int x,int y,int n,uint16_t c){expected.put(x,y,n,c);}));
  RasterJob job;assert(job.begin_ghost(art,blink,center,center));
  assert(job.step(0,[](int,int,int,uint16_t){assert(false);}).work==0);
  const auto total=drain(job,actual,1+(cases%127));assert(job.phase()==RasterPhase::complete);
  assert(total==2*watcher_art::sprites[state][blink?1:0].count);assert(actual.spans==expected.spans);assert(actual.pixels==expected.pixels);++cases;
 }
 for(unsigned state=0;state<9;++state)for(bool blink:{false,true})for(unsigned shape=0;shape<4;++shape)for(unsigned scale:{32u,66u,128u,255u,256u})for(unsigned hue:{0u,137u,270u,359u}){
  const int x=(cases%3==0)?-10:(cases%3==1)?233:465;
  const int y=(cases%4==0)?-20:(cases%4==1)?215:465;
  const auto alpha=static_cast<uint8_t>((cases%3==0)?0:(cases%3==1)?113:255);
  Image expected,actual;
  assert(draw_personality(static_cast<State>(state),blink,x,y,scale,hue,shape,[&](int xx,int yy,int n,uint16_t c){expected.put(xx,yy,n,carousel_color(c,0x0842,alpha));}));
  RasterJob job;assert(job.begin_personality(static_cast<State>(state),blink,x,y,scale,hue,shape));
  const auto total=drain(job,actual,1+(cases%257),alpha);assert(job.phase()==RasterPhase::complete);
  assert(total==2*watcher_art::sprites[state][blink?1:0].count);assert(actual.spans==expected.spans);assert(actual.pixels==expected.pixels);++cases;
 }
 // A layered scene verifies overlapping order/alpha across independent paused jobs.
 Image layeredExpected,layeredActual;
 for(unsigned i=0;i<4;++i){
  const auto state=static_cast<State>(i);const unsigned hue=83*i;const uint8_t alpha=static_cast<uint8_t>(64+i*60);
  assert(draw_personality(state,false,200+10*i,210,200,hue,i,[&](int x,int y,int n,uint16_t c){layeredExpected.put(x,y,n,carousel_color(c,0x0842,alpha));}));
  RasterJob job;assert(job.begin_personality(state,false,200+10*i,210,200,hue,i));drain(job,layeredActual,3,alpha);
 }
 assert(layeredActual.pixels==layeredExpected.pixels&&layeredActual.spans==layeredExpected.spans);
 std::vector<watcher_art::Run> transparent(watcher_art::height,{static_cast<uint16_t>(0x8000|watcher_art::width),0xffff});
 RasterJob blank;Image empty;assert(blank.begin_sprite({transparent.data(),transparent.size()},233,215));assert(drain(blank,empty,1)==2*transparent.size());assert(blank.phase()==RasterPhase::complete&&empty.spans.empty());
 auto bad=transparent;bad.back().tag=0;malformed({bad.data(),bad.size()});
 bad=transparent;bad.back().tag=watcher_art::width+1;malformed({bad.data(),bad.size()});
 malformed({transparent.data(),transparent.size()-1});malformed({nullptr,1});malformed({transparent.data(),0});
 malformed({transparent.data(),watcher_art::width*watcher_art::height+1});
 RasterJob reuse;Image reused;assert(reuse.begin_ghost(State::idle,false,233,215));reuse.step(1,[](int,int,int,uint16_t){assert(false);});reuse.cancel();assert(reuse.phase()==RasterPhase::idle&&!reuse.active());
 assert(!reuse.begin_personality(State::idle,false,233,215,31,270,0));assert(!reuse.begin_personality(State::idle,false,233,215,257,270,0));
 assert(!reuse.begin_personality(State::idle,false,233,215,66,360,0));assert(!reuse.begin_personality(State::idle,false,233,215,66,270,4));
 assert(!reuse.begin_ghost(static_cast<State>(9),false,233,215));assert(!reuse.begin_ghost(State::idle,false,40000,0));
 assert(reuse.begin_ghost(State::idle,false,32767,32767));drain(reuse,reused,1);assert(reuse.phase()==RasterPhase::complete&&reused.spans.empty());
 // Copied descriptor, immutable borrowed storage: descriptor changes cannot shorten job.
 watcher_art::Sprite descriptor{transparent.data(),transparent.size()};assert(reuse.begin_sprite(descriptor,233,215));descriptor.count=0;assert(drain(reuse,reused,9)==2*transparent.size());
 std::cout<<cases<<" raster equivalence cases, layering, visited-work bounds, malformed data and cancellation pass\n";
}
