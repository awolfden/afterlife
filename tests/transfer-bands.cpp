#include "../firmware/render/transfer-bands.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>
using namespace watcher_render;
int main(){
 unsigned cases=0;
 for(unsigned rotation=0;rotation<4;++rotation)for(unsigned rows=2;rows<=64;rows+=2){
  TransferBands plan;TransferBand band;assert(plan.begin(rotation,rows));assert(!plan.complete());
  std::vector<unsigned char> coverage(466*466,0);unsigned bands=0;
  while(plan.next(band)){
   assert(++bands<=234);assert(band.x>=0&&band.y>=0&&band.width>0&&band.height>0&&band.x+band.width<=466&&band.y+band.height<=466);
   assert(!(band.physical_y&1)&&!(band.physical_rows&1)&&band.physical_rows<=static_cast<int>(rows));
   int minX=468,minY=468,maxX=-1,maxY=-1;
   for(int y=band.y;y<band.y+band.height;++y)for(int x=band.x;x<band.x+band.width;++x){
    assert(++coverage[y*466+x]==1);
    // Independent per-pixel counterpart of pinned _rotate_pixelcopy.
    int px=x,py=y;
    switch(rotation){case 1:px=467-y;py=x;break;case 2:px=467-x;py=467-y;break;case 3:px=y;py=467-x;break;default:break;}
    assert(px>=0&&px<468&&py>=band.physical_y&&py<band.physical_y+band.physical_rows);
    minX=std::min(minX,px);minY=std::min(minY,py);maxX=std::max(maxX,px);maxY=std::max(maxY,py);
   }
   // Even-expanding flush still fits this physical band and reserved468px row.
   minX&=~1;minY&=~1;maxX|=1;maxY|=1;
   assert(minY>=band.physical_y&&maxY<band.physical_y+band.physical_rows);
   assert(minX>=0&&maxX<468&&maxX-minX+1<=468);
  }
  assert(plan.complete());for(auto value:coverage)assert(value==1);
  assert(!plan.next(band)&&band.width==0&&band.height==0);++cases;
 }
 TransferBands bad;TransferBand band;
 for(unsigned rows:{0u,1u,3u,65u,66u,~0u}){assert(!bad.begin(0,rows));assert(!bad.next(band)&&!bad.complete());}
 assert(!bad.begin(4,16)&&!bad.next(band));assert(!bad.begin(~0u,16));
 assert(bad.begin(3,16)&&bad.next(band));assert(bad.begin(0,2)&&bad.next(band));assert(band.x==0&&band.y==0&&band.height==2);
 std::cout<<cases<<" rotation/band coverage cases preserve every pixel and even-expanded physical row bounds\n";
}
