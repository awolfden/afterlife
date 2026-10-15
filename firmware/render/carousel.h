// Display-only circular paging. No gesture, transport, ACK or voice authority.
#pragma once
#include <stdint.h>
namespace watcher_render {
struct CarouselScene {
 uint32_t boot=0,generation=0,source=0,rotation=0,count=0,selected=0,roster=0,selection_epoch=0;
};
struct CarouselPose {int x=233,y=240,scale=66,depth=0;unsigned alpha=0;};
class Carousel {
 static constexpr int32_t unit=65536;
 static constexpr int64_t duration=200;
 CarouselScene scene_{};
 int32_t phase_=0,from_=0,target_=0,anchor_=0;
 int64_t began_=0,last_=0;
 bool initialized_=false,moving_=false,preview_=false;
 static bool valid(const CarouselScene& s){return s.boot&&s.roster&&s.selection_epoch&&s.count>=2&&s.count<=16&&s.selected<s.count&&s.source<=2&&s.rotation<4;}
 bool compatible(const CarouselScene& s)const{return scene_.boot==s.boot&&scene_.generation==s.generation&&scene_.source==s.source&&scene_.rotation==s.rotation&&scene_.count==s.count&&scene_.roster==s.roster;}
 static int32_t mix(int32_t a,int32_t b,uint32_t weight){return a+static_cast<int32_t>(static_cast<int64_t>(b-a)*weight/unit);}
 void sample(int64_t now){
  if(!moving_)return;
  if(now-began_>=duration){phase_=target_;moving_=false;return;}
  const uint32_t r=unit-static_cast<uint32_t>((now-began_)*unit/duration);
  const uint32_t square=static_cast<uint32_t>((static_cast<uint64_t>(r)*r)>>16);
  phase_=mix(from_,target_,unit-static_cast<uint32_t>((static_cast<uint64_t>(square)*square)>>16));
 }
 // 64 segments per turn with fixed-point interpolation. No per-frame trig,
 // heap, frame copies, accumulated angular drift or long wrapping loops.
 static int sine(int32_t angle){
  static constexpr int table[]={0,3212,6393,9512,12539,15446,18204,20787,23170,25329,27245,28898,30273,31356,32137,32609,32767};
  const unsigned turn=static_cast<uint16_t>(angle),quadrant=turn/16384;
  unsigned x=turn%16384;if(quadrant&1)x=16384-x;
  const unsigned cell=x/1024,part=x%1024;
  const int value=cell==16?table[16]:table[cell]+(table[cell+1]-table[cell])*static_cast<int>(part)/1024;
  return quadrant>=2?-value:value;
 }
 public:
 void reset(){initialized_=moving_=preview_=false;phase_=from_=target_=anchor_=0;}
 bool moving()const{return initialized_&&(moving_||preview_);}
 bool previewing()const{return initialized_&&preview_;}
 // release: 0=no event, 1=awaiting host, 2=host consumed this gesture event.
 bool update(const CarouselScene& next,int64_t now,bool animate,bool dragging=false,int dx=0,unsigned release=0,int direction=0){
  if(now<0||!valid(next)){reset();return false;}
  const bool cold=!initialized_;
  if(cold||now<last_||!compatible(next)||!animate||(scene_.selection_epoch!=next.selection_epoch&&release!=2)){
   reset();scene_=next;initialized_=true;last_=began_=now;
   if(!cold||!animate||!dragging)return true;
  }
  sample(now);
  if(scene_.selected!=next.selected){
   // Only reconcile an actual one-step host result. Unsolicited navigation or
   // missing/replaced identities snap rather than borrowing unknown neighbors.
   int step=0;
   if(next.selected==(scene_.selected+1)%next.count)step=1;
   if(next.selected==(scene_.selected+next.count-1)%next.count){
    // In a two-item ring both neighbors have the same identity; retain the
    // gesture direction instead of reversing at the acknowledgement boundary.
    if(next.count!=2||phase_<0)step=-1;
   }
   if(next.count==2&&(direction==1||direction==-1))step=direction;
   const int32_t rebased=phase_-step*unit;
   if(release!=2||!step||(direction&&direction!=step)||rebased < -unit||rebased > unit){reset();scene_=next;initialized_=true;last_=began_=now;return true;}
   phase_=rebased;from_=phase_;target_=0;moving_=false;preview_=true;
  }
  scene_=next;last_=now;
  if(dragging){
   if(!preview_)anchor_=phase_; // Catch a settling ring without jumping home.
   const int distance=dx<-100?-100:dx>100?100:dx;
   const int magnitude=distance<0?-distance:distance;
   const uint32_t weight=magnitude>6?static_cast<uint32_t>((magnitude-6)*unit/94):0;
   phase_=mix(anchor_,distance<0?unit:-unit,weight);
   preview_=true;moving_=false;return true;
  }
  if(preview_){
   if(release==1)return true;
   from_=phase_;target_=0;began_=now;preview_=false;moving_=phase_!=0;
  }
  return true;
 }
 CarouselPose pose(unsigned index)const{
  if(!initialized_||index>=scene_.count)return {};
  const int32_t period=static_cast<int32_t>(scene_.count)*unit,half=period/2;
  int32_t d=(static_cast<int32_t>(index)-static_cast<int32_t>(scene_.selected))*unit-phase_;
  d=((d+half)%period+period)%period-half;
  const int32_t magnitude=d<0?-d:d;
  if(scene_.count>4&&magnitude>=2*unit)return {};
  const int32_t angle=d/static_cast<int32_t>(scene_.count<4?scene_.count:4);
  const int horizontal=sine(angle),depth=sine(angle+16384),front=depth>0?depth:0;
  // Front grows, all rear ghosts stay small. Aggregate raster area does not
  // exceed the old one-large/three-small resting scene.
  const int scale=scene_.count==2?66+114*(depth+32767)/(2*32767):66+static_cast<int>(114LL*front*front/(32767LL*32767));
  unsigned alpha=255;
  if(scene_.count>4&&magnitude>3*unit/2)alpha=static_cast<unsigned>((2*unit-magnitude)*255/(unit/2));
  return {233+horizontal*124/32767,240-depth*56/32767,scale,depth,alpha};
 }
 unsigned order(unsigned (&indices)[4])const{
  unsigned n=0;
  if(!initialized_)return n;
  for(unsigned i=0;i<scene_.count;i++)if(pose(i).alpha){
   if(n==4)return 0; // Defensive bound; never emit a partial oversized scene.
   indices[n++]=i;
  }
  for(unsigned i=1;i<n;i++)for(unsigned j=i;j>0&&pose(indices[j]).depth<pose(indices[j-1]).depth;j--){const auto t=indices[j];indices[j]=indices[j-1];indices[j-1]=t;}
  return n;
 }
};
// The fading rear-most ghost is drawn first over the cleared background, before
// opaque ghosts. Blending spans needs no per-pixel reads or extra canvas.
inline uint16_t carousel_color(uint16_t color,uint16_t background,unsigned alpha){
 if(alpha>=255)return color;
 const unsigned inverse=255-alpha;
 const unsigned r=(((color>>11)&31)*alpha+((background>>11)&31)*inverse)/255;
 const unsigned g=(((color>>5)&63)*alpha+((background>>5)&63)*inverse)/255;
 const unsigned b=((color&31)*alpha+(background&31)*inverse)/255;
 return static_cast<uint16_t>((r<<11)|(g<<5)|b);
}
} // namespace watcher_render
