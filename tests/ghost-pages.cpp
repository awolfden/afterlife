#include <cassert>
#include <initializer_list>
#include <cstdio>
#include <cstring>
#include <vector>
#include <set>
#include "../firmware/pet/main/ghost-wire.h"
#include "../firmware/render/ghost.h"
#include "../firmware/pet/main/orientation.h"
static size_t checked(char* out,const char* prefix){const size_t n=std::strlen(prefix);assert(n<175);std::memcpy(out,prefix,n);return n+std::snprintf(out+n,192-n,"%u",watcher_wire::crc(out,n));}
int main(int argc,char**){
 char line[192];watcher_ghost::Row row{};
 if(argc>1){
  unsigned parsed=0;
  while(std::fgets(line,sizeof line,stdin)){
   const size_t size=std::strlen(line);assert(size&&line[size-1]=='\n');
   assert(watcher_ghost::parse(line,size-1,row));
   std::printf("%u %u %u %s\n",row.boot,row.seq,row.index,row.title);parsed++;
  }
  assert(parsed);return 0;
 }
 auto n=checked(line,"G1 1 2 0 3 1 1 200 2 1 48656c6c6f ");
 assert(watcher_ghost::parse(line,n,row));assert(!std::strcmp(row.title,"Hello"));
 watcher_wire::Frame frame{1,2,0,1,3,1,0,0,0,0};watcher_ghost::Page page;
 assert(page.accept(row,frame));assert(!page.accept(row,frame));assert(page.complete(frame));assert(page.get(0,frame));
 frame.seq=3;assert(!page.get(0,frame));assert(!page.accept(row,frame));assert(!page.complete(frame));
 for(const char* bad:{"00","1b","c080","eda080","f4908080","e280ae","f09f90"}){
  char prefix[160];std::snprintf(prefix,sizeof prefix,"G1 1 2 0 3 1 1 200 2 1 %s ",bad);n=checked(line,prefix);assert(!watcher_ghost::parse(line,n,row));
 }
 n=checked(line,"G1 1 2 0 3 2 1 200 2 1 48 ");assert(!watcher_ghost::parse(line,n,row));
 n=checked(line,"G1 1 2 0 3 1 1 200 2 1 f09f90ba ");assert(watcher_ghost::parse(line,n,row));line[5]='4';assert(!watcher_ghost::parse(line,n,row));
 // A complete page survives jitter; partial traffic cannot renew its display hold.
 watcher_ghost::Page group;watcher_wire::Frame grouped{1,10,0,4,0,0,0,0,0,0};
 watcher_ghost::Row member{};member.boot=1;member.seq=10;
 for(unsigned i=0;i<4;i++){member.index=i;assert(group.accept(member,grouped,100));assert(group.complete(grouped)==(i==3));}
 assert(group.mode(grouped,900)==watcher_ghost::Mode::carousel);
 for(int64_t at=200;at<=1700;at+=100){
  grouped.seq++;member.seq=grouped.seq;
  for(unsigned i=0;i<3;i++){member.index=i;assert(group.accept(member,grouped,at));}
  assert(!group.complete(grouped));
  assert(group.mode(grouped,at)==(at<=1600?watcher_ghost::Mode::waiting:watcher_ghost::Mode::legacy));
 }
 grouped.seq++;grouped.selected=4;grouped.count=5;member.seq=grouped.seq;member.index=4;
 assert(group.accept(member,grouped,1800));assert(group.complete(grouped));
 member.index=5;assert(!group.accept(member,grouped,1800));
 grouped.boot=2;assert(group.mode(grouped,1801)==watcher_ghost::Mode::legacy);
 for(unsigned count_value:{0u,17u}){char prefix[160];std::snprintf(prefix,sizeof prefix,"G1 1 2 0 3 1 1 200 2 %u 48 ",count_value);n=checked(line,prefix);assert(!watcher_ghost::parse(line,n,row));}
 unsigned count=0;
 for(unsigned state=0;state<9;state++)for(unsigned scale:{32u,66u,190u,256u})for(int center:{-40,0,233,465,510}){
  assert(watcher_render::draw_personality(static_cast<watcher_render::State>(state),false,center,center,scale,200,[&](int x,int y,int length,uint16_t){assert(x>=0&&y>=0&&x+length<=466&&y<466&&length>0);count++;}));
 }
 assert(count>100);assert(watcher_render::tint(0xffff,30)==0xffff);assert(watcher_render::tint(0,30)==0);
 // Actual decoded art bounds, not estimates: bob extremes retain an 8px vertical gap.
 int focus_bottom=-1,small_top=466;
 for(unsigned state=0;state<9;state++)for(bool blink:{false,true}){
  watcher_render::draw_personality(static_cast<watcher_render::State>(state),blink,233,184+5,180,200,[&](int,int y,int,uint16_t){if(y>focus_bottom)focus_bottom=y;});
  watcher_render::draw_personality(static_cast<watcher_render::State>(state),blink,233,302-4,66,200,[&](int,int y,int,uint16_t){if(y<small_top)small_top=y;});
 }
 assert(small_top-focus_bottom>=8);
 assert(!watcher_render::multi_session_layout(0));assert(!watcher_render::multi_session_layout(1));
 assert(watcher_render::multi_session_layout(2));assert(watcher_render::multi_session_layout(5)); // A one-row fifth-session page is still multi.
 for(unsigned state=0;state<9;state++)for(bool blink:{false,true}){
  std::vector<uint32_t> original(466*466),purple(466*466);
  auto pixels=[](std::vector<uint32_t>& image,int x,int y,int length,uint16_t color){for(int i=0;i<length;i++)image[y*466+x+i]=static_cast<uint32_t>(color)+1;};
  watcher_render::draw_ghost(static_cast<watcher_render::State>(state),blink,233,215,[&](int x,int y,int n,uint16_t c){pixels(original,x,y,n,c);});
  watcher_render::draw_personality(static_cast<watcher_render::State>(state),blink,233,215,256,270,0,[&](int x,int y,int n,uint16_t c){pixels(purple,x,y,n,c);});
  assert(original==purple); // Purple means exact approved artwork, not a hue approximation.
  int bottom=-1,top=466;std::set<uint64_t> silhouettes;
  for(unsigned trait=0;trait<4;trait++){
   uint64_t shape=1469598103934665603ULL;
   const auto check_circle=[](int x,int y,int n){for(const int xx:{x,x+n-1})assert((xx-233)*(xx-233)+(y-233)*(y-233)<=225*225);assert(y<340);};
   watcher_render::draw_personality(static_cast<watcher_render::State>(state),blink,235,189,180,270,trait,[&](int x,int y,int n,uint16_t){check_circle(x,y,n);if(y>bottom)bottom=y;shape=(shape^static_cast<unsigned>(x+y*466+n*466*466))*1099511628211ULL;});
   silhouettes.insert(shape);
   for(const int xx:{231,235})for(const int yy:{179,189})watcher_render::draw_personality(static_cast<watcher_render::State>(state),blink,xx,yy,180,270,trait,[&](int x,int y,int n,uint16_t){check_circle(x,y,n);});
   for(const int xx:{131,133,232,234,333,335})for(const int yy:{298,306})watcher_render::draw_personality(static_cast<watcher_render::State>(state),blink,xx,yy,66,270,trait,[&](int x,int y,int n,uint16_t){check_circle(x,y,n);});
   for(const int xx:{131,233,335})watcher_render::draw_personality(static_cast<watcher_render::State>(state),blink,xx,298,66,270,trait,[&](int x,int y,int n,uint16_t){check_circle(x,y,n);if(y<top)top=y;});
   for(unsigned s=0;s<9;s++)for(int64_t now=0;now<8000;now+=137){
    const auto pose=watcher_render::personality_pose(static_cast<watcher_render::State>(s),false,now,trait,false);
    assert(std::abs(pose.x)<=2&&std::abs(pose.y)<=5);if(s>=7)assert(!pose.x&&!pose.y&&!pose.blink);
    const auto reduced=watcher_render::personality_pose(static_cast<watcher_render::State>(s),true,now,trait,false);assert(!reduced.x&&!reduced.y&&!reduced.blink);
   }
  }
  assert(silhouettes.size()==4);assert(top-bottom>=8);
 }
 watcher_orientation::Settled orientation;int64_t at=0;
 // Physical regression: vertical signs swapped, sideways signs unchanged.
 const float axes[4][2]={{0,-1},{1,0},{0,1},{-1,0}};
 for(unsigned desired=0;desired<4;desired++){
  const unsigned before=orientation.current();
  for(unsigned step=0;step<9;step++,at+=50){const auto value=orientation.update(axes[desired][0],axes[desired][1],0,at);assert(value==(step<8?before:desired));}
 }
 assert(orientation.update(0,0,1,at)==3);at+=50;
 assert(orientation.update(NAN,0,0,at)==3);at+=50;
 assert(orientation.update(0.71f,0.71f,0,at)==3);at+=50;
 assert(orientation.update(2,0,0,at)==3);at+=50;
 orientation.update(0,1,0,at);at+=500;assert(orientation.update(0,1,0,at)==3);
 std::puts("Ghost parser, exact-generation page, malformed UTF8, scaled rendering and settled orientation pass");
}
