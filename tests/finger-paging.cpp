#include "../firmware/pet/main/swipe.h"
#include "../firmware/pet/main/ghost-wire.h"
#include "../firmware/pet/main/buttons.h"
#include "../firmware/pet/main/paging-controls.h"
#include "../firmware/render/focus-transition.h"
#include "../firmware/render/ghost.h"
#include <cassert>
#include <climits>
#include <cstdio>
#include <string>
using namespace watcher_render;
static FocusScene scene(unsigned count=4,unsigned selected=1){FocusScene s;s.boot=1;s.count=count;s.selected=selected;s.roster=1;s.selection_epoch=1;for(unsigned i=0;i<4;i++){s.hue[i]=i*80;s.rhythm[i]=i;}return s;}
static bool eq(FocusPose a,FocusPose b){return a.x==b.x&&a.y==b.y&&a.scale==b.scale;}
static std::string record(unsigned boot,unsigned seq,unsigned roster,unsigned selection){
 auto p=std::string("K1 ")+std::to_string(boot)+" "+std::to_string(seq)+" "+std::to_string(roster)+" "+std::to_string(selection)+" ";return p+std::to_string(watcher_wire::crc(p.data(),p.size()));
}
int main(int argc,char**){
 if(argc>1){ // Cross-language parser mode, one valid focus record per W1.
  char line[192];watcher_ghost::FocusContext context;watcher_wire::Frame f{};f.boot=99;
  while(std::fgets(line,sizeof line,stdin)){std::string text(line);if(text.back()=='\n')text.pop_back();++f.seq;assert(context.accept(text.data(),text.size(),f));std::printf("%u %u\n",context.roster,context.selection);}return 0;
 }
 static_assert(sizeof(FocusTransition)<=256);
 static_assert(sizeof(watcher_ghost::FocusContext)==16);
 // Every same-page endpoint including same-page wraps, with exact reversal.
 for(unsigned count=2;count<=16;count++)for(unsigned selected=0;selected<count;selected++)for(int sign:{-1,1}){
  auto s=scene(count,selected);FocusTransition m,rest,target;
  rest.update(s,0,false);m.update(s,0,true);
  auto dest=s;dest.selected=sign<0?(selected+1)%count:(selected+count-1)%count;
  const bool known=selected/4==dest.selected/4;
  target.update(known?dest:s,0,false);
  const unsigned visible=count-selected/4*4<4?count-selected/4*4:4;
  FocusPose halfway[4];
  m.reset();int64_t now=0;
  for(int dx:{0,5,6,30,70,100,70,30,6,0}){
   assert(m.update(s,now+=20,true,true,sign*dx));assert(m.previewing());
   for(unsigned i=0;i<visible;i++){
    auto p=m.pose(i);assert(p.x>=132&&p.x<=334&&p.y>=184&&p.y<=302&&p.scale>=66&&p.scale<=180);
    if(dx<=6||!known)assert(eq(p,rest.pose(i)));
    if(dx==100)assert(eq(p,target.pose(i)));
    if(dx==30){if(now<100)halfway[i]=p;else assert(eq(p,halfway[i]));}
    assert(draw_personality(State::idle,false,p.x,p.y,p.scale,s.hue[i],s.rhythm[i],[](int x,int y,int n,uint16_t){assert(x>=0&&y>=0&&n>0&&x+n<=466&&y<466);}));
   }
   unsigned order[4];m.order(order);unsigned mask=0;
   for(unsigned i=0;i<visible;i++){assert(order[i]<visible);mask|=1u<<order[i];if(i)assert(m.pose(order[i-1]).scale<=m.pose(order[i]).scale);}assert(mask==(1u<<visible)-1);
  }
  // Cancellation starts at the shown geometry, settles home, never selects.
  m.update(s,220,true,true,sign*70);const auto p=m.pose(selected%4);
  m.update(s,240,true);assert(eq(m.pose(selected%4),p));
  m.update(s,440,true);assert(!m.moving());for(unsigned i=0;i<visible;i++)assert(eq(m.pose(i),rest.pose(i)));
  // No imagined cross-page ghosts. Ordinary release navigation still works.
  m.update(s,460,true,true,sign*80);const auto held=m.pose(selected%4);
  m.update(s,480,true,false,0,1);m.update(s,900,true,false,0,1);assert(eq(held,m.pose(selected%4)));
  dest.selection_epoch++;m.update(dest,920,true,false,0,2);
  if(known)assert(eq(held,m.pose(selected%4)));else assert(!m.moving());
  m.update(dest,1120,true);assert(!m.moving());
 }
 // Catching a return in flight starts at sampled current geometry, not home.
 {auto s=scene();FocusTransition m,reference;
  for(auto* motion:{&m,&reference}){motion->update(s,0,true,true,-70);motion->update(s,20,true);}
  reference.update(s,60,true);m.update(s,60,true,true,0);
  FocusPose anchor[4];for(unsigned i=0;i<4;i++){anchor[i]=m.pose(i);assert(eq(anchor[i],reference.pose(i)));}
  m.update(s,80,true,true,40);m.update(s,100,true,true,0);
  for(unsigned i=0;i<4;i++)assert(eq(anchor[i],m.pose(i)));
  m.update(s,120,true,true,-100);FocusTransition goal;auto target=s;target.selected=2;goal.update(target,0,false);
  for(unsigned i=0;i<4;i++)assert(eq(m.pose(i),goal.pose(i)));
 }
 // Opposite-direction catches at a nontrivial ease weight retain the anchor,
 // have monotone scale, reach either neighbor, and still cancel to home.
 for(int sign:{-1,1}){
  auto s=scene();FocusTransition m,reference,rest;rest.update(s,0,false);
  for(auto* motion:{&m,&reference}){motion->update(s,0,true,true,-70);motion->update(s,20,true);}
  reference.update(s,120,true);m.update(s,120,true,true,0);FocusPose anchor[4];
  for(unsigned i=0;i<4;i++){anchor[i]=reference.pose(i);assert(eq(m.pose(i),anchor[i]));}
  for(int dx:{-6,6,0}){m.update(s,140,true,true,dx);for(unsigned i=0;i<4;i++)assert(eq(m.pose(i),anchor[i]));}
  int scale=m.pose(1).scale;
  for(int dx=7;dx<=100;dx++){m.update(s,140+dx,true,true,sign*dx);assert(m.pose(1).scale<=scale);scale=m.pose(1).scale;}
  auto dest=s;dest.selected=sign<0?2:0;FocusTransition target;target.update(dest,0,false);
  for(unsigned i=0;i<4;i++)assert(eq(m.pose(i),target.pose(i)));
  m.update(s,260,true);m.update(s,460,true);for(unsigned i=0;i<4;i++)assert(eq(m.pose(i),rest.pose(i)));
 }
 // Interrupt an accepted-release animation, anchored in its new authority.
 {auto s=scene();FocusTransition m,reference;
  for(auto* motion:{&m,&reference})motion->update(s,0,true,true,-70);
  s.selected=2;s.selection_epoch++;
  for(auto* motion:{&m,&reference})motion->update(s,20,true,false,0,2);
  reference.update(s,80,true);m.update(s,80,true,true,0);
  for(unsigned i=0;i<4;i++)assert(eq(m.pose(i),reference.pose(i)));
  m.update(s,100,true,true,100);auto dest=s;dest.selected=1;FocusTransition target;target.update(dest,0,false);
  for(unsigned i=0;i<4;i++)assert(eq(m.pose(i),target.pose(i)));
 }
 // Ordinary committed-selection animation is not an ambiguous preview:
 // short ACK remains usable; the retired longer voice chord stays inert.
 for(unsigned hold:{100u,700u}){
  auto s=scene();FocusTransition motion;motion.update(s,0,true);s.selected=2;
  motion.update(s,20,true);assert(motion.moving()&&!motion.previewing());
  watcher_buttons::Buttons buttons;watcher_touch::PreviewButtons gate;unsigned ack=0;
  for(int64_t t=0;t<static_cast<int64_t>(hold)+200;t+=10){
   const unsigned raw=t>=50&&t<50+hold?3:0;
   const bool blocked=gate.update(raw,t,motion.previewing());assert(!blocked);
   auto e=buttons.update(raw,t,1,1);
   ack+=e.action==watcher_buttons::Action::ack;
  }
  assert(ack==(hold==100?1u:0u));
 }
 // Host rejection settles to origin rather than inventing a successful change.
 {auto s=scene();FocusTransition m,rest;rest.update(s,0,false);m.update(s,0,true,true,-70);auto p=m.pose(1);
  m.update(s,20,true,false,0,1);m.update(s,40,true,false,0,2);assert(eq(p,m.pose(1)));m.update(s,240,true);assert(eq(m.pose(1),rest.pose(1)));
 }
 // Identical traits do not hide roster/selection replacement; all faults snap.
 for(unsigned fault=0;fault<9;fault++){
  auto s=scene();FocusTransition m,rest;m.update(s,0,true,true,-70);bool animate=true;int64_t now=20;
  switch(fault){case 0:s.roster++;break;case 1:s.selection_epoch++;break;case 2:s.boot++;break;case 3:s.source=2;break;case 4:s.generation++;break;case 5:s.rotation++;break;case 6:animate=false;break;case 7:now=-1;break;case 8:m.update(s,100,true,true,-70);break;}
  bool ok=m.update(s,now,animate);assert(ok==(now>=0));assert(!m.moving());
  if(ok){rest.update(s,0,false);for(unsigned i=0;i<4;i++)assert(eq(m.pose(i),rest.pose(i)));}
 }
 {FocusTransition m;auto s=scene();for(int dx:{INT_MIN,INT_MAX}){assert(m.update(s,0,true,true,dx));for(unsigned i=0;i<4;i++)assert(m.pose(i).scale>=66&&m.pose(i).scale<=180);}m.update(s,1,false,true,100);assert(!m.moving());}
 // Exact frame-bound identity parsing; stale/duplicate/invalid records fail.
 {watcher_ghost::FocusContext c;watcher_wire::Frame f{};f.boot=1;f.seq=1;
  auto take=[&](std::string text){return c.accept(text.data(),text.size(),f);};
  assert(take(record(1,1,1,1)));assert(c.matches(f));assert(!take(record(1,1,2,2)));
  f.seq=2;assert(!c.matches(f));assert(!take(record(2,2,2,2)));assert(!take(record(1,3,2,2)));assert(!take(record(1,2,0,2)));
  auto bad=record(1,2,2,2);bad.back()=bad.back()=='0'?'1':'0';assert(!take(bad));assert(!take(record(1,2,2,2)+" "));
  assert(take(record(1,2,2,2)));f.seq=3;assert(!take(record(1,3,1,2)));assert(!take(record(1,3,2,1)));
  f.boot=2;f.seq=1;assert(take(record(2,1,1,1)));
 }
 // Preview-started ACK gestures are suppressed even after cancellation
 // has restored the normal scene; ordinary button paging remains immediate.
 for(unsigned value:{1u,2u,3u})for(unsigned hold:{100u,700u,1300u}){
  watcher_buttons::Buttons buttons;watcher_touch::PreviewButtons gate;unsigned ack=0,pages=0,mute=0;
  for(int64_t t=0;t<static_cast<int64_t>(hold)+200;t+=10){
   const unsigned raw=t>=50&&t<50+hold?value:0;
   const bool blocked=gate.update(raw,t,t==50);
   auto e=buttons.update(raw,t,1,1);
   if(blocked&&e.action==watcher_buttons::Action::ack)e.action=watcher_buttons::Action::none;
   ack+=e.action==watcher_buttons::Action::ack;
   pages+=e.action==watcher_buttons::Action::previous||e.action==watcher_buttons::Action::next;mute+=e.action==watcher_buttons::Action::mute;
  }
  assert(!ack);assert(pages==(value==3?0u:1u));assert(mute==(value==3&&hold==1300?1u:0u));
  assert(!gate.update(0,hold+300,false));
 }
 std::puts("finger paging: distance, reversal, settling, identity, depth, raster and control quarantine pass");
}
