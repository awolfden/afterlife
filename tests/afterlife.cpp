#include "../firmware/render/retained-frame.h"
#include <vector>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <string>
#define CHECK(x) do{if(!(x)){std::cerr<<__LINE__<<": " #x "\n";std::abort();}}while(false)
using namespace watcher_render;
struct Surface {
 std::vector<uint16_t> pixels=std::vector<uint16_t>(466*466,0);
 PaintRect area{0,0,466,466};unsigned writes=0;std::string label;
 void clip(PaintRect r){area=r;}
 void clear(PaintRect r,uint16_t c){for(int y=r.y;y<r.bottom;++y)for(int x=r.x;x<r.right;++x)pixels[y*466+x]=c;}
 void span(int x,int y,int n,uint16_t c){CHECK(n>0&&x>=area.x&&x+n<=area.right&&y>=area.y&&y<area.bottom);++writes;for(int i=0;i<n;++i)pixels[y*466+x+i]=c;}
 void shadow(){}void circle(int,int,int,uint16_t,bool){}
 void text(const char* s,int y,float size,uint16_t){if(y==414){CHECK(size==2.0f&&std::string(s)=="Settings");return;}CHECK(y==340&&size==2.5f);label=s;}
 void text_background(const char* s,int y,float size,uint16_t c,uint16_t bg){CHECK(bg==0x1063||bg==c);text(s,y,size,c);}
};
int main(int argc,char** argv){
#ifndef WATCHER_AFTERLIFE
 (void)argc;(void)argv;SceneProgram scene;CHECK(!scene.afterlife()&&!scene.valid());std::cout<<"Disabled profile rejects Afterlife scene\n";
#else
 CHECK(argc==2);SpriteRows art;CHECK(art.initialize()&&art.initialize()&&sizeof(art)==20236);CHECK(valid_afterlife_art());
 SceneProgram offline;CHECK(offline.afterlife());CHECK(offline.text("Connect to Pi",340,2.5f,0xe71e,0x1063));
 SceneProgram different;CHECK(different.afterlife()&&different.text("Connect to Pi",340,2.5f,0xe71e));CHECK(!same_paint(offline,1,different,1));CHECK(paint_bounds(offline.op(0)).area()==466*466);
 SceneJob legacy;CHECK(!legacy.begin(offline));
 RetainedFrame retained;Surface actual;CHECK(retained.prepare(offline)&&retained.paint(art,0x0842,actual)&&retained.commit());CHECK(actual.label=="Connect to Pi");
 std::ofstream out(argv[1],std::ios::binary);for(auto p:actual.pixels){char b[]={static_cast<char>(p),static_cast<char>(p>>8)};out.write(b,2);}CHECK(out.good());out.close();
 const auto saved=actual.pixels;const auto writes=actual.writes;
 CHECK(retained.prepare(offline)&&retained.dirty().empty()&&retained.paint(art,0x0842,actual)&&retained.commit());CHECK(actual.pixels==saved&&actual.writes==writes&&retained.visited_runs()==0);
 for(unsigned i=0;i<60;++i){SceneProgram online;CHECK(online.ghost(State::idle,false,static_cast<int>(i*29%600)-60,215));CHECK(retained.prepare(online)&&retained.dirty().area()==466*466&&retained.paint(art,0x0842,actual)&&retained.commit());RetainedFrame fresh;Surface expected;CHECK(fresh.prepare(online)&&fresh.paint(art,0x0842,expected)&&fresh.commit());CHECK(actual.pixels==expected.pixels);CHECK(retained.prepare(offline)&&retained.dirty().area()==466*466&&retained.paint(art,0x0842,actual)&&retained.commit());CHECK(actual.pixels==saved);}
 // Direct row clipping used by partial retained damage.
 for(const PaintRect clip: {PaintRect{0,0,1,1},PaintRect{127,119,342,363},PaintRect{465,465,466,466}}){Surface clipped;clipped.clip(clip);uint32_t visited=0;draw_afterlife(clip.x,clip.y,clip.right,clip.bottom,clipped,visited);CHECK(visited);for(int y=0;y<466;++y)for(int x=0;x<466;++x)CHECK(clipped.pixels[y*466+x]==((x>=clip.x&&x<clip.right&&y>=clip.y&&y<clip.bottom)?saved[y*466+x]:0));}
 for(const int64_t base:{int64_t{0},int64_t{2592000000}})for(int64_t n=0;n<2400;++n){
  const auto t=base+n*33;const auto p=connecting_pose(t,false),next=connecting_pose(t+33,false),still=connecting_pose(t,true);
  CHECK(p.x>=201&&p.x<=265&&p.y>=387&&p.y<=403&&p.dots<=3);
  CHECK(std::abs(p.x-next.x)<=2&&std::abs(p.y-next.y)<=2);
  CHECK(still.x==233&&still.y==395&&still.dots==3&&!still.blink);
 }
 CHECK(connecting_pose(-1,false).x==233);
 for(unsigned i=0;i<120;++i){
  const auto p=connecting_pose(i*177,false);SceneProgram connecting;char label[]="Connecting to Pi   ";for(unsigned n=0;n<p.dots;++n)label[16+n]='.';
  CHECK(connecting.afterlife()&&connecting.text(label,340,2.5f,0xe71e,0xe71e)&&connecting.family_ghost(State::idle,p.blink,p.x,381+(p.y-395)/4,0,32)&&connecting.text("Settings",414,2.0f,0xe71e,0xe71e));
  CHECK(retained.prepare(connecting)&&retained.paint(art,0x0842,actual)&&retained.commit());RetainedFrame fresh;Surface expected;CHECK(fresh.prepare(connecting)&&fresh.paint(art,0x0842,expected)&&fresh.commit());CHECK(actual.pixels==expected.pixels);
 }
 std::ofstream loader(std::string(argv[1])+".loader",std::ios::binary);for(auto p:actual.pixels){char b[]={static_cast<char>(p),static_cast<char>(p>>8)};loader.write(b,2);}CHECK(loader.good());loader.close();
 CHECK(retained.prepare(offline)&&retained.paint(art,0x0842,actual)&&retained.commit()&&actual.pixels==saved);
 std::cout<<"Exact native artwork decode; 120 online/offline transitions; 120 loader composites; 4800 bounded continuous poses; no-op/reduced-motion reuse; clipped rows; native prompt; no added index RAM\n";
#endif
}
