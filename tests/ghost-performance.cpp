#include "../firmware/render/ghost.h"
#include <array>
#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <vector>
using watcher_render::State;
// Frozen pre-optimization personality kernel. Shared lower-level art decode and
// tint are intentionally unchanged by this optimization and reviewed separately.
template<class Sink>
bool reference(State state,bool blink,int center_x,int center_y,unsigned scale,unsigned hue,unsigned archetype,Sink&& sink){
 if(scale<32||scale>256||hue>359||archetype>3)return false;
 const int widths[]={100,100,88,94},heights[]={100,90,100,94};
 const int xscale=static_cast<int>(scale)*widths[archetype],yscale=static_cast<int>(scale)*heights[archetype];
 return watcher_render::draw_ghost(state,blink,watcher_art::origin_x,watcher_art::origin_y,[&](int x,int y,int n,uint16_t color){
  const int dy=y-static_cast<int>(watcher_art::origin_y),lean=archetype==3?dy*4/100:0;
  int left=center_x+(x-static_cast<int>(watcher_art::origin_x)+lean)*xscale/25600;
  int right=center_x+(x+n-static_cast<int>(watcher_art::origin_x)+lean)*xscale/25600;
  int top=center_y+dy*yscale/25600;
  int bottom=center_y+(dy+1)*yscale/25600;
  if(left<0)left=0;
  if(right>static_cast<int>(watcher_render::logical_width))right=watcher_render::logical_width;
  if(top<0)top=0;
  if(bottom>static_cast<int>(watcher_render::logical_height))bottom=watcher_render::logical_height;
  if(right>left)for(int yy=top;yy<bottom;yy++)sink(left,yy,right-left,watcher_render::tint(color,hue));
 });
}
struct Span {int x,y,n;uint16_t color;bool operator==(const Span& b)const{return x==b.x&&y==b.y&&n==b.n&&color==b.color;}};
struct Sum {uint64_t value=0;void operator()(int x,int y,int n,uint16_t c){value=value*1099511628211ULL+uint64_t(x)+uint64_t(y)*466+uint64_t(n)*217156+c;}};
template<bool optimized>static std::pair<long long,uint64_t> bench(unsigned companions){
 Sum sum;const auto start=std::chrono::steady_clock::now();
 for(unsigned f=0;f<400;f++){
  for(unsigned i=0;i<=companions;i++){
   const unsigned hues[]={270,190,35,335};const auto state=static_cast<State>((f/20+i)%9);const unsigned scale=i?66:180;const int x=i?132+101*int(i-1):233,y=i?302:184;
   if constexpr(optimized)watcher_render::draw_personality(state,f%7==0,x,y,scale,hues[i],i,sum);
   else reference(state,f%7==0,x,y,scale,hues[i],i,sum);
  }
 }
 return {std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count(),sum.value};
}
int main(int argc,char** argv){
 if(argc==2&&std::string(argv[1])=="--bench"){
  std::cout<<"[";bool first=true;
  for(unsigned companions:{1u,3u}){
   for(unsigned repeat=0;repeat<5;repeat++){
    const auto before=bench<false>(companions),after=bench<true>(companions);assert(before.second==after.second);
    if(!first)std::cout<<",";
    first=false;std::cout<<"{\"companions\":"<<companions<<",\"referenceUs\":"<<before.first<<",\"candidateUs\":"<<after.first<<",\"equalOutputChecksum\":true}";
   }
  }
  std::cout<<"]\n";return 0;
 }
 uint64_t cases=0,spans=0;
 for(unsigned state=0;state<10;state++)for(bool blink:{false,true})for(unsigned trait=0;trait<5;trait++)for(unsigned scale:{31u,32u,66u,180u,256u,257u})for(unsigned hue:{0u,59u,60u,119u,120u,179u,180u,239u,240u,269u,270u,299u,300u,359u,360u})for(const auto& center:std::array<std::array<int,2>,5>{{{{233,184}},{{132,302}},{{335,298}},{{-25,-20}},{{480,480}}}}){
  std::vector<Span> a,b;const auto sinkA=[&](int x,int y,int n,uint16_t c){a.push_back({x,y,n,c});};const auto sinkB=[&](int x,int y,int n,uint16_t c){b.push_back({x,y,n,c});};
  const auto old=reference(static_cast<State>(state),blink,center[0],center[1],scale,hue,trait,sinkA),now=watcher_render::draw_personality(static_cast<State>(state),blink,center[0],center[1],scale,hue,trait,sinkB);
  assert(old==now&&a==b);cases++;spans+=a.size();
 }
 std::cout<<"{\"cases\":"<<cases<<",\"exactSpansCompared\":"<<spans<<"}\n";
}
