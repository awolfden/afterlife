// Local visual evidence from the actual portable renderer, not a device capture.
#include "../firmware/render/ghost.h"
#include <cassert>
#include <fstream>
#include <string>
#include <vector>
int main(int argc,char** argv){
 assert(argc==2);using watcher_render::State;
 constexpr unsigned hues[]={270,165,45,205};
 for(unsigned scene=0;scene<5;scene++){
  std::vector<uint16_t> pixels(466*466,0x0842);
  auto sink=[&](int x,int y,int n,uint16_t color){assert(x>=0&&y>=0&&x+n<=466&&y<466);for(int i=0;i<n;i++)pixels[y*466+x+i]=color;};
  if(!scene)watcher_render::draw_ghost(State::idle,false,233,215,sink);
  else{
   const unsigned focused=scene-1;const auto state=static_cast<State>(focused==0?0:focused==1?2:focused==2?3:5);
   const auto pose=watcher_render::personality_pose(state,false,1700,focused,false);
   watcher_render::draw_personality(state,pose.blink,233+pose.x,184+pose.y,180,hues[focused],focused,sink);
   unsigned slot=0;
   for(unsigned i=0;i<4;i++)if(i!=focused){const auto p=watcher_render::personality_pose(State::idle,false,1700,i,true);watcher_render::draw_personality(State::idle,p.blink,132+101*slot++ +p.x,302+p.y,66,hues[i],i,sink);}
  }
  std::ofstream out(std::string(argv[1])+"/scene-"+std::to_string(scene)+".rgb",std::ios::binary);
  for(const auto c:pixels){const char rgb[]={static_cast<char>(((c>>11)&31)*255/31),static_cast<char>(((c>>5)&63)*255/63),static_cast<char>((c&31)*255/31)};out.write(rgb,3);}assert(out.good());
 }
}
