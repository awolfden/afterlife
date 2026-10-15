#include "../firmware/render/focus-transition.h"
#include "../firmware/render/ghost.h"
#include <cassert>
#include <climits>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace watcher_render;
static FocusScene scene(unsigned count,unsigned selected){FocusScene s;s.boot=1;s.count=count;s.selected=selected;for(unsigned i=0;i<4;i++){s.hue[i]=(270+i*67)%360;s.rhythm[i]=i;}return s;}
static bool equal(FocusPose a,FocusPose b){return a.x==b.x&&a.y==b.y&&a.scale==b.scale;}
static FocusPose goal(unsigned selected,unsigned slot){if(selected==slot)return {233,184,180};return {132+static_cast<int>(slot-(slot>selected?1:0))*101,302,66};}
static void raster(const FocusTransition& motion,const FocusScene& s,std::vector<uint16_t>& pixels,bool baseline=false){
 const unsigned count=s.count-s.selected/4*4<4?s.count-s.selected/4*4:4;
 unsigned order[4];motion.order(order);
 if(baseline){order[0]=s.selected%4;unsigned n=1;for(unsigned i=0;i<count;i++)if(i!=order[0])order[n++]=i;}
 for(unsigned n=0;n<count;n++){
  const unsigned i=order[n];const auto at=baseline?goal(s.selected%4,i):motion.pose(i);
  assert(draw_personality(State::idle,false,at.x,at.y,at.scale,s.hue[i],s.rhythm[i],[&](int x,int y,int size,uint16_t color){
   assert(x>=0&&y>=0&&size>0&&x+size<=466&&y<466);
   for(int p=x;p<x+size;p++)pixels[y*466+p]=color;
  }));
 }
}
int main(int argc,char** argv){
 static_assert(sizeof(FocusTransition)<=256,"Bounded persistent presentation state");
 for(unsigned count=2;count<=16;count++)for(unsigned selected=0;selected<count;selected++){
  const unsigned start=selected/4*4,n=count-start<4?count-start:4;
  FocusTransition motion;auto s=scene(count,selected);assert(motion.update(s,0,true));assert(!motion.moving());
  for(unsigned i=0;i<n;i++)assert(equal(motion.pose(i),goal(selected%4,i)));
  std::vector<uint16_t> actual(466*466,0x0842),expected=actual;raster(motion,s,actual);raster(motion,s,expected,true);assert(actual==expected);
  for(unsigned target=start;target<start+n;target++){
   motion.reset();s=scene(count,selected);motion.update(s,0,true);const auto first=motion.pose(selected%4);
   s.selected=target;motion.update(s,10,true);assert(equal(motion.pose(selected%4),first));
   int previous=motion.pose(target%4).scale;
   for(int t=1;t<=200;t++){
    assert(motion.update(s,10+t,true));
    for(unsigned i=0;i<n;i++){const auto p=motion.pose(i);assert(p.x>=132&&p.x<=334&&p.y>=184&&p.y<=302&&p.scale>=66&&p.scale<=180);}
    assert(motion.pose(target%4).scale>=previous);previous=motion.pose(target%4).scale;
    unsigned order[4];motion.order(order);unsigned mask=0;
    for(unsigned i=0;i<n;i++){assert(order[i]<n);mask|=1u<<order[i];if(motion.moving()&&i)assert(motion.pose(order[i-1]).scale<=motion.pose(order[i]).scale);}
    assert(mask==(1u<<n)-1);
   }
   assert(!motion.moving());for(unsigned i=0;i<n;i++)assert(equal(motion.pose(i),goal(target%4,i)));
  }
 }
 // Retarget continuously from the sampled geometry, not from the previous endpoint.
 FocusTransition a,b;auto s=scene(4,0);a.update(s,0,true);b.update(s,0,true);s.selected=1;a.update(s,10,true);b.update(s,10,true);
 a.update(s,75,true);s.selected=2;b.update(s,75,true);for(unsigned i=0;i<4;i++)assert(equal(a.pose(i),b.pose(i)));
 b.update(s,275,true);assert(equal(b.pose(2),goal(2,2)));
 // Context changes snap. No identity/content from an old group is rendered.
 for(unsigned change=0;change<10;change++){
  FocusTransition m;auto c=scene(4,0);m.update(c,0,true);c.selected=1;m.update(c,10,true);m.update(c,40,true);
  bool animate=true;int64_t now=50;
  switch(change){case 0:c.boot++;break;case 1:c.generation++;break;case 2:c.source=2;break;case 3:c.rotation=1;break;case 4:c.count=3;break;case 5:c.hue[0]++;break;case 6:c.rhythm[0]=2;break;case 7:animate=false;break;case 8:now=1;break;case 9:m.reset();break;}
  m.update(c,now,animate);assert(!m.moving());for(unsigned i=0;i<(c.count<4?c.count:4);i++)assert(equal(m.pose(i),goal(1,i)));
 }
 FocusTransition m;s=scene(8,1);m.update(s,0,true);s.selected=5;m.update(s,10,true);assert(!m.moving());
 s=scene(4,0);m.reset();m.update(s,INT64_MAX-500,true);s.selected=1;m.update(s,INT64_MAX-400,true);m.update(s,INT64_MAX,true);assert(!m.moving());
 assert(!m.update(s,-1,true));assert(!m.moving());s.selected=4;assert(!m.update(s,1,true));
 s=scene(4,0);s.count=17;assert(!m.update(s,1,true));s=scene(4,0);s.hue[0]=360;assert(!m.update(s,1,true));
 // Reduced motion clears an in-progress transition, with no replay on re-enable.
 s=scene(4,0);m.update(s,0,true);s.selected=1;m.update(s,1,true);m.update(s,50,false);assert(!m.moving());m.update(s,60,true);assert(!m.moving());
 // Raster frames for every 50 ms render opportunity; optional PPM contact sheet.
 std::vector<uint16_t> sheet(5*466*466,0x0842);m.reset();s=scene(2,0);m.update(s,0,true);s.selected=1;
 for(unsigned step=0;step<5;step++){
  m.update(s,step*50,true);std::vector<uint16_t> pixels(466*466,0x0842);raster(m,s,pixels);
  for(unsigned y=0;y<466;y++)for(unsigned x=0;x<466;x++)sheet[y*(5*466)+step*466+x]=pixels[y*466+x];
 }
 if(argc==3&&!std::strcmp(argv[1],"--preview")){
  FILE* out=std::fopen(argv[2],"wb");assert(out);std::fprintf(out,"P6\n2330 466\n255\n");
  for(auto color:sheet){const unsigned char rgb[]={static_cast<unsigned char>(((color>>11)&31)*255/31),static_cast<unsigned char>(((color>>5)&63)*255/63),static_cast<unsigned char>((color&31)*255/31)};assert(std::fwrite(rgb,1,3,out)==3);}std::fclose(out);
 }
 std::printf("focus transition: endpoints, retargets, context resets, reduced motion, depth and raster bounds pass; state=%zu bytes\n",sizeof(FocusTransition));
}
