#include "../firmware/render/ghost.h"
#include <cassert>
#include <climits>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
int main(int argc,char** argv) {
  assert(argc==2);
  unsigned calls=0,checks=0;
  const auto sink=[&](int x,int y,int n,uint16_t){assert(x>=0 && y>=0 && n>0 && x+n<=466 && y<466);++calls;};
  for(unsigned s=0;s<9;s++)for(unsigned b=0;b<2;b++) {
    const auto state=static_cast<watcher_render::State>(s);
    for(const int16_t x:{int16_t(SHRT_MIN),int16_t(-1),int16_t(0),int16_t(233),int16_t(466),int16_t(SHRT_MAX)})
      for(const int16_t y:{int16_t(SHRT_MIN),int16_t(-1),int16_t(0),int16_t(215),int16_t(466),int16_t(SHRT_MAX)}) {
        assert(watcher_render::draw_ghost(state,b,x,y,sink));++checks;
      }
    std::vector<uint8_t> decoded(280*280*3,0);
    unsigned visible=0;
    assert(watcher_render::draw_ghost(state,b,140,130,[&](int x,int y,int n,uint16_t color){
      assert(x+n<=280 && y<280);
      for(int i=0;i<n;i++){const size_t at=(y*280+x+i)*3;assert(!decoded[at]);decoded[at]=1;decoded[at+1]=color&255;decoded[at+2]=color>>8;++visible;}
    }));assert(visible>10000 && visible<60000);
    const std::string path=std::string(argv[1])+"/frame-"+std::to_string(s*2+b)+".raw";
    std::ofstream output(path,std::ios::binary);output.write(reinterpret_cast<const char*>(decoded.data()),decoded.size());assert(output.good());
  }
  calls=0;assert(!watcher_render::draw_ghost(static_cast<watcher_render::State>(255),false,233,215,sink));assert(!calls);
  const watcher_art::Run zero[]={{0,0}},overflow[]={{281,0}},short_image[]={{280,0}};
  for(const watcher_art::Sprite bad: {watcher_art::Sprite{nullptr,1},{zero,1},{overflow,1},{short_image,1},{short_image,78401}}) {
    assert(!watcher_render::draw_sprite(bad,233,215,sink));assert(!calls);++checks;
  }
  std::vector<watcher_art::Run> trailing(281,{280,0});
  assert(!watcher_render::draw_sprite({trailing.data(),trailing.size()},233,215,sink));assert(!calls);++checks;
  std::cout<<checks<<" clipping/malformed-data scenarios; 18 decoded frames written\n";
}
