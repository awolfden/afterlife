#include "../firmware/render/carousel.h"
#include "../firmware/render/ghost.h"
#include "../firmware/pet/main/ghost-wire.h"
#include "../firmware/pet/main/swipe.h"
#include "../firmware/pet/main/buttons.h"
#include "../firmware/pet/main/paging-controls.h"
#include <cassert>
#include <climits>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace watcher_render;
static CarouselScene scene(unsigned count,unsigned selected){return {1,0,1,0,count,selected,1,1};}
static bool same(CarouselPose a,CarouselPose b){return a.x==b.x&&a.y==b.y&&a.scale==b.scale&&a.depth==b.depth&&a.alpha==b.alpha;}
static watcher_wire::Frame frame(unsigned count,unsigned selected,unsigned seq=1){return {1,seq,selected,count,0,0,0,0,0,0};}
static bool neighbor(unsigned i,const watcher_wire::Frame& f){const unsigned d=(i+f.count-f.selected)%f.count;return f.count<=5||d<=2||d>=f.count-2;}
static void fill(watcher_ghost::Page& page,const watcher_wire::Frame& f,int64_t now,bool legacy=false){
 unsigned n=0;
 for(unsigned i=0;i<f.count;i++)if(i/4==f.selected/4||(!legacy&&neighbor(i,f))){
  watcher_ghost::Row row;row.boot=f.boot;row.seq=f.seq;row.index=i;row.hue=i*20;row.space_count=1;
  assert(page.accept(row,f,now));assert(!page.accept(row,f,now));++n;
 }
 assert(n<=6);assert(page.complete(f));
}
static void bounds(const Carousel& c,unsigned count,const watcher_ghost::Page& page,const watcher_wire::Frame& f,bool raster){
 unsigned order[4];const unsigned n=c.order(order);assert(n>=2&&n<=4);unsigned mask=0,area=0,faded=0;
 for(unsigned k=0;k<n;k++){
  const unsigned i=order[k];assert(i<count&&!(mask&(1u<<i)));mask|=1u<<i;assert(page.get(i,f));
  const auto p=c.pose(i);assert(p.alpha>0&&p.alpha<=255&&p.scale>=66&&p.scale<=180&&p.x>=109&&p.x<=357&&p.y>=184&&p.y<=296);
  if(k)assert(c.pose(order[k-1]).depth<=p.depth);
  if(p.alpha<255){++faded;assert(k==0);} // Background blend must precede every opaque ghost.
  area+=p.scale*p.scale;
  if(raster)for(unsigned trait=0;trait<4;trait++)for(int bob:{-5,5}){
   assert(draw_personality(State::needs_input,false,p.x+2,p.y+bob,p.scale,i*20,trait,[&](int x,int y,int length,uint16_t color){
    assert(x>=0&&x+length<=466&&y>=100&&y<334&&length>0);
    for(int xx:{x,x+length-1})assert((xx-233)*(xx-233)+(y-233)*(y-233)<=225*225);
    (void)carousel_color(color,0x0842,p.alpha);
   }));
  }
 }
 assert(faded<=1);
 assert(area<=180*180+(count<4?count-1:3)*66*66);
 for(unsigned i=0;i<count;i++)assert(bool(c.pose(i).alpha)==bool(mask&(1u<<i)));
}
int main(int argc,char** argv){
 if(argc==2&&!std::strcmp(argv[1],"--rows")){
  // Host-produced corpus: W1 then union G1 rows for each count/selection.
  watcher_wire::Frame f{};watcher_ghost::Page page;char line[192];unsigned cases=0;
  auto finish=[&](){if(f.seq){assert(page.complete(f)&&page.ring_complete(f));assert(page.mode(f,0)==watcher_ghost::Mode::carousel);++cases;}};
  while(std::fgets(line,sizeof line,stdin)){
   const size_t length=std::strlen(line);assert(length&&line[length-1]=='\n');
   if(!std::strncmp(line,"W1 ",3)){finish();assert(watcher_wire::parse(line,length-1,f));}
   else{watcher_ghost::Row row;assert(watcher_ghost::parse(line,length-1,row));assert(page.accept(row,f,0));}
  }
  finish();std::printf("%u ring windows accepted\n",cases);return 0;
 }
 static_assert(sizeof(Carousel)<=128,"Scalar angular state stays bounded");
 static_assert(sizeof(watcher_ghost::Page)<=512,"No whole-roster or old-frame cache");
 for(unsigned count=2;count<=16;count++)for(unsigned selected=0;selected<count;selected++)for(int direction:{-1,1}){
  auto s=scene(count,selected);Carousel c,home,goal;home.update(s,0,false);c.update(s,0,true);
  auto target=s;target.selected=(selected+count+direction)%count;target.selection_epoch++;
  goal.update(target,0,false);
  watcher_ghost::Page page;auto f=frame(count,selected);fill(page,f,0);assert(page.ring_complete(f));
  CarouselPose half[16];int64_t now=0;
  for(int magnitude:{0,6,30,70,100,70,30,6,0}){
   c.update(s,now+=20,true,true,-direction*magnitude);
   bounds(c,count,page,f,count<=4);
   for(unsigned i=0;i<count;i++){
    if(magnitude<=6)assert(same(c.pose(i),home.pose(i)));
    if(magnitude==100)assert(same(c.pose(i),goal.pose(i)));
    if(magnitude==30){if(now<100)half[i]=c.pose(i);else assert(same(c.pose(i),half[i]));}
   }
  }
  // All three ghosts move, including the previously stationary companion.
  if(count==3){c.update(s,200,true,true,-direction*53);for(unsigned i=0;i<count;i++)assert(!same(c.pose(i),home.pose(i)));}
  // Cancel and host rejection settle on the same angular path, without selecting.
  c.update(s,220,true,true,-direction*70);const auto shown=c.pose(selected);
  c.update(s,240,true);assert(same(shown,c.pose(selected)));
  c.update(s,440,true);assert(!c.moving());for(unsigned i=0;i<count;i++)assert(same(c.pose(i),home.pose(i)));
  // Accepted release across EVERY boundary, including old four-item pages and wrap.
  c.update(s,460,true,true,-direction*70);CarouselPose held[16];for(unsigned i=0;i<count;i++)held[i]=c.pose(i);
  c.update(s,480,true,false,0,1,direction);c.update(s,900,true,false,0,1,direction);
  for(unsigned i=0;i<count;i++)assert(same(held[i],c.pose(i)));
  c.update(target,920,true,false,0,2,direction);
  for(unsigned i=0;i<count;i++)assert(same(held[i],c.pose(i)));
  f=frame(count,target.selected,2);fill(page,f,920);
  for(int t=940;t<=1120;t+=20){c.update(target,t,true);bounds(c,count,page,f,false);}
  for(unsigned i=0;i<count;i++)assert(same(c.pose(i),goal.pose(i)));
  assert(!c.moving());
 }
 // Feed actual recognizer output into presentation: no authority before the
 // confirmed release, one event only, origin identity retained through wrap.
 for(unsigned count:{2u,3u,4u,5u,16u})for(unsigned selected=0;selected<count;selected++)for(int direction:{-1,1}){
  watcher_touch::Swipe swipe;watcher_touch::Context context{true,1,10,selected,count,0,1,0,1,1};
  auto s=scene(count,selected);Carousel c;c.update(s,0,true);
  swipe.update({watcher_touch::Kind::released},context,0);swipe.update({watcher_touch::Kind::released},context,20);
  for(int step=0;step<3;step++){
   const auto event=swipe.update({watcher_touch::Kind::contact,116-direction*40*step,100},context,40+40*step);
   assert(!event.kind&&s.selected==selected);c.update(s,40+40*step,true,swipe.tracking(),swipe.displacement());
  }
  assert(!swipe.update({watcher_touch::Kind::released},context,160).kind);
  const auto event=swipe.update({watcher_touch::Kind::released},context,180);assert(event.kind==(direction==1?2u:1u)&&event.shown==10);
  c.update(s,180,true,false,0,1,direction);assert(c.previewing());
  assert(!swipe.update({watcher_touch::Kind::released},context,200).kind);
  s.selected=(selected+count+direction)%count;s.selection_epoch++;
  c.update(s,220,true,false,0,2,direction);assert(!c.previewing());c.update(s,420,true);assert(!c.moving());
  assert(c.pose(s.selected).scale==180);
 }
 // Dense fractional phases: <=4 unique ghosts and bounded area, rear fade,
 // display circle/text clearance, and +/-2 metadata cover through reversal.
 for(unsigned count:{3u,4u,5u,16u}){
  auto s=scene(count,0);Carousel c;watcher_ghost::Page p;auto f=frame(count,0);fill(p,f,0);
  for(int dx=-100;dx<=100;dx++){c.update(s,dx+100,true,true,dx);bounds(c,count,p,f,true);}
 }
 // Interruption retains a single angular anchor, and sign reversal returns to it.
 for(bool accepted:{false,true}){
  auto s=scene(5,4);Carousel c,reference;
  for(auto* m:{&c,&reference})m->update(s,0,true,true,-70);
  if(accepted){s.selected=0;s.selection_epoch++;}
  for(auto* m:{&c,&reference})m->update(s,20,true,false,0,accepted?2:0,1);
  reference.update(s,100,true);c.update(s,100,true,true,0);CarouselPose anchor[5];
  for(unsigned i=0;i<5;i++){anchor[i]=reference.pose(i);assert(same(anchor[i],c.pose(i)));}
  c.update(s,120,true,true,40);c.update(s,140,true,true,-6);
  for(unsigned i=0;i<5;i++)assert(same(anchor[i],c.pose(i)));
  c.update(s,160,true,true,-100);auto next=s;next.selected=(s.selected+1)%5;Carousel goal;goal.update(next,0,false);
  for(unsigned i=0;i<5;i++)assert(same(goal.pose(i),c.pose(i)));
 }
 // Exact identity/context changes never reuse an old ring, even for equal traits.
 for(unsigned fault=0;fault<10;fault++){
  auto s=scene(5,4);Carousel c,rest;c.update(s,0,true,true,-70);int64_t now=20;bool animate=true;
  switch(fault){case 0:s.boot++;break;case 1:s.roster++;break;case 2:s.selection_epoch++;break;case 3:s.count=6;break;case 4:s.rotation++;break;case 5:s.source=2;break;case 6:s.generation++;break;case 7:animate=false;break;case 8:now=-1;break;case 9:c.update(s,100,true,true,-70);break;}
  const bool ok=c.update(s,now,animate,true,-70);assert(ok==(now>=0));assert(!c.moving());
  if(ok){rest.update(s,0,false);for(unsigned i=0;i<s.count;i++)assert(same(c.pose(i),rest.pose(i)));}
 }
 {Carousel c;auto s=scene(3,0);s.roster=0;assert(!c.update(s,0,true));s=scene(3,0);s.selected=3;assert(!c.update(s,0,true));s=scene(17,0);assert(!c.update(s,0,true));}
 {Carousel c;auto s=scene(3,0);for(int dx:{INT_MIN,INT_MAX})assert(c.update(s,0,true,true,dx));c.update(s,1,false,true,100);assert(!c.moving());
  c.update(s,INT64_MAX-500,true,true,-70);c.update(s,INT64_MAX-400,true);c.update(s,INT64_MAX,true);assert(!c.moving());}
 // Missing ring rows cannot renew a hold indefinitely through complete legacy pages.
 {watcher_ghost::Page p;auto f=frame(8,0);fill(p,f,0);assert(p.mode(f,0)==watcher_ghost::Mode::carousel);
  for(int now=100;now<=1600;now+=100){++f.seq;fill(p,f,now,true);assert(!p.ring_complete(f));assert(p.mode(f,now)==(now<=1500?watcher_ghost::Mode::waiting:watcher_ghost::Mode::group));}
  ++f.seq;fill(p,f,1700);assert(p.mode(f,1700)==watcher_ghost::Mode::carousel);
  f.boot++;assert(p.mode(f,1710)==watcher_ghost::Mode::legacy);
 }
 // Unsolicited rows cannot fill the cache or substitute a different identity.
 {watcher_ghost::Page p;auto f=frame(16,0);watcher_ghost::Row r;r.boot=1;r.seq=1;r.index=8;assert(!p.accept(r,f));fill(p,f,0);assert(!p.get(8,f));r.index=0;r.seq=2;assert(!p.accept(r,f));}
 assert(carousel_color(0xffff,0x0842,0)==0x0842);assert(carousel_color(0xffff,0x0842,255)==0xffff);
 if(argc==3&&!std::strcmp(argv[1],"--preview")){
  // Counts 2,3,4,5; columns show one next-step orbit while the finger stays down.
  constexpr unsigned columns=5,rows=4,width=466*columns,height=466*rows;
  std::vector<uint16_t> image(width*height,0x0842);
  for(unsigned row=0;row<rows;row++){
   auto s=scene(row+2,row+1);Carousel c;
   for(unsigned col=0;col<columns;col++){
    c.update(s,col*20,true,true,-static_cast<int>(col*25));unsigned order[4];const unsigned n=c.order(order);
    for(unsigned k=0;k<n;k++){const auto i=order[k];const auto p=c.pose(i);
     draw_personality(State::idle,false,p.x,p.y,p.scale,(270+i*67)%360,i%4,[&](int x,int y,int length,uint16_t color){
      for(int xx=x;xx<x+length;xx++)image[(row*466+y)*width+col*466+xx]=carousel_color(color,0x0842,p.alpha);
     });
    }
   }
  }
  FILE* file=std::fopen(argv[2],"wb");assert(file);std::fprintf(file,"P6\n%u %u\n255\n",width,height);
  for(const auto color:image){const unsigned char rgb[]={static_cast<unsigned char>(((color>>11)&31)*255/31),static_cast<unsigned char>(((color>>5)&63)*255/63),static_cast<unsigned char>((color&31)*255/31)};assert(std::fwrite(rgb,1,3,file)==3);}std::fclose(file);
 }
 std::printf("carousel: 2..16 sessions, circular paths, reversal, wrap, reconciliation, metadata bounds, depth and raster pass; state=%zu cache=%zu bytes\n",sizeof(Carousel),sizeof(watcher_ghost::Page));
}
