#define WATCHER_GHOST_FAMILY 1
#include "../firmware/render/retained-frame.h"
#include "../firmware/pet/main/ghost-wire.h"
#include <vector>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <string>
#define CHECK(x) do{if(!(x)){std::cerr<<__LINE__<<": " #x "\n";std::abort();}}while(false)
using namespace watcher_render;
struct Surface {
 std::vector<uint16_t> pixels=std::vector<uint16_t>(466*466,0x0842);
 std::vector<bool> touched=std::vector<bool>(466*466,false);
 PaintRect clipper{0,0,466,466};
 void clip(PaintRect r){clipper=r;}
 void clear(PaintRect r,uint16_t color){for(int y=r.y;y<r.bottom;++y)for(int x=r.x;x<r.right;++x){pixels[y*466+x]=color;touched[y*466+x]=false;}}
 void span(int x,int y,int n,uint16_t color){CHECK(n>0&&x>=clipper.x&&x+n<=clipper.right&&y>=clipper.y&&y<clipper.bottom);for(int i=0;i<n;++i){pixels[y*466+x+i]=color;touched[y*466+x+i]=true;}}
 void shadow(){}void circle(int,int,int,uint16_t,bool){}void text(const char*,int,float,uint16_t){}
};
static std::string wire(const std::string& prefix){return prefix+std::to_string(watcher_wire::crc(prefix.data(),prefix.size()));}
int main(int argc,char** argv){
 CHECK(argc==2);SpriteRows art;CHECK(art.initialize()&&art.initialize());CHECK(sizeof(art)<=21000);
 CHECK(valid_family_art());unsigned decoded=0;
 for(unsigned c=0;c<6;++c)for(unsigned s=0;s<9;++s)for(unsigned b=0;b<2;++b){
  SceneProgram scene;CHECK(scene.family_ghost(static_cast<State>(s),b,140,130,c));Surface surface;uint32_t visits=0;
  CHECK(art.draw(scene.op(0),{0,0,466,466},0x0842,surface,visits)&&visits>0);
  std::ofstream file(std::string(argv[1])+"/"+std::to_string(c)+"-"+std::to_string(s)+"-"+std::to_string(b)+".rgb",std::ios::binary);
  for(unsigned y=0;y<280;++y)for(unsigned x=0;x<280;++x){const auto i=y*466+x;const auto color=surface.touched[i]?surface.pixels[i]:0;const char bytes[]={static_cast<char>(surface.touched[i]),static_cast<char>(color&255),static_cast<char>(color>>8)};file.write(bytes,3);}
  CHECK(file.good());++decoded;
 }
 RetainedFrame retained;Surface incremental;
 unsigned comparisons=0;
 for(unsigned step=0;step<180;++step){
  SceneProgram scene;const unsigned c=step%6,scale=32+(step*37)%225;
  CHECK(scene.family_ghost(static_cast<State>(step%9),step%2,static_cast<int>((step*19)%600)-60,static_cast<int>((step*29)%600)-60,c,scale,64+(step*31)%192));
  CHECK(scene.family_ghost(static_cast<State>((step+3)%9),false,233,210,(c+2)%6,180,180));
  CHECK(retained.prepare(scene)&&retained.paint(art,0x0842,incremental)&&retained.commit());
  RetainedFrame fresh;Surface full;CHECK(fresh.prepare(scene)&&fresh.paint(art,0x0842,full)&&fresh.commit());CHECK(incremental.pixels==full.pixels);
  CHECK(retained.prepare(scene)&&retained.dirty().empty()&&retained.paint(art,0x0842,incremental)&&retained.commit());++comparisons;
 }
 SceneProgram a,b;CHECK(a.family_ghost(State::idle,false,233,215,0));CHECK(b.family_ghost(State::idle,false,233,215,1));CHECK(!same_paint(a,0,b,0));
 SceneProgram invalid;CHECK(!invalid.family_ghost(State::idle,false,233,215,6)&&!invalid.valid());
 SceneJob legacy;CHECK(!legacy.begin(a)&&legacy.phase()==ScenePhase::failed);
 watcher_ghost::Row row;const auto g1=wire("G1 7 10 0 0 0 0 270 0 1 74657374 ");CHECK(watcher_ghost::parse(g1.data(),g1.size(),row)&&!row.family);
 for(unsigned c=0;c<6;++c){const auto g2=wire("G2 7 10 0 0 0 0 270 0 1 "+std::to_string(c)+" 74657374 ");CHECK(watcher_ghost::parse(g2.data(),g2.size(),row)&&row.family&&row.character==c);watcher_wire::Frame frame{};frame.boot=7;frame.seq=10;frame.count=1;watcher_ghost::Page page;CHECK(page.accept(row,frame,0)&&!page.accept(row,frame,1)&&page.mode(frame,0)==watcher_ghost::Mode::carousel);frame.seq=11;CHECK(!page.accept(row,frame,1));}
 for(const auto& prefix:{"G2 7 10 0 0 0 0 270 0 1 6 74657374 ","G2 7 10 0 0 0 0 270 0 1 4294967295 74657374 ","G2 7 10 0 0 0 0 270 0 1 74657374 ","G2 7 10 0 0 0 0 270 0 1 0 74657374 3 "}){const auto bad=wire(prefix);CHECK(!watcher_ghost::parse(bad.data(),bad.size(),row));}
 auto bad=wire("G2 7 10 0 0 0 0 270 0 1 1 74657374 ");bad.back()=bad.back()=='0'?'1':'0';CHECK(!watcher_ghost::parse(bad.data(),bad.size(),row));
 std::cout<<decoded<<" exact decoded frames; "<<comparisons<<" clipped, scaled, alpha retained/full comparisons; immutable index RAM "<<sizeof(art)<<" bytes\n";
}
