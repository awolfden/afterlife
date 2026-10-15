#include "../firmware/render/motion-pager.h"
#include "../firmware/render/retained-frame.h"
#include "../firmware/render/settings-scene.h"
#include <cstdlib>
#include <iostream>
#include <vector>
#include <string>
#include <stdexcept>
#define CHECK(e) do{if(!(e)){std::cerr<<__LINE__<<": " #e "\n";std::abort();}}while(false)
using namespace watcher_render;
constexpr uint16_t bg=0x0842;
struct Surface {
 std::vector<uint16_t> pixels=std::vector<uint16_t>(466*466,bg);
 PaintRect region{0,0,466,466};unsigned clears=0;bool throws=false;
 void clip(PaintRect r){region=r;}
 void pixel(int x,int y,uint16_t c){if(x>=region.x&&x<region.right&&y>=region.y&&y<region.bottom&&x>=0&&y>=0&&x<466&&y<466)pixels[y*466+x]=c;}
 void span(int x,int y,int n,uint16_t c){if(throws)throw std::runtime_error("surface");for(int i=0;i<n;++i)pixel(x+i,y,c);}
 void clear(PaintRect r,uint16_t c){++clears;for(int y=r.y;y<r.bottom;++y)for(int x=r.x;x<r.right;++x)pixel(x,y,c);}
 void shadow(){for(int y=311;y<330;++y)span(173,y,121,0x28e7);}
 void circle(int x,int y,int r,uint16_t c,bool filled){for(int yy=-r;yy<=r;++yy)for(int xx=-r;xx<=r;++xx){int d=xx*xx+yy*yy;if(d<=r*r&&(filled||d>(r-1)*(r-1)))pixel(x+xx,y+yy,c);}}
 void text(const char* v,int y,float size,uint16_t c){const int w=static_cast<int>(std::strlen(v)*6*size);for(int yy=0;yy<static_cast<int>(8*size);++yy)for(int x=233-w/2;x<233+w/2;++x)if((x+yy)%3)pixel(x,y+yy,c);}
};
static void reference(const SceneProgram& program,Surface& s){
 s.clip({0,0,466,466});s.clear(s.region,bg);
 for(unsigned i=0;i<program.count();++i){const auto& op=program.op(i);
  auto sink=[&](int x,int y,int n,uint16_t c){s.span(x,y,n,carousel_color(c,bg,op.alpha));};
  switch(op.kind){
  case SceneKind::afterlife:CHECK(false);break; // Legacy reference cannot render Afterlife.
  case SceneKind::ghost:if(op.personality)CHECK(draw_personality(static_cast<State>(op.state),op.blink,op.x,op.y,op.scale,op.hue,op.archetype,sink));else CHECK(draw_ghost(static_cast<State>(op.state),op.blink,op.x,op.y,sink));break;
  case SceneKind::shadow:s.shadow();break;
  case SceneKind::filled_circle:case SceneKind::outline_circle:s.circle(op.x,op.y,op.radius,op.color,op.kind==SceneKind::filled_circle);break;
  case SceneKind::text:s.clip({0,op.clip_top,466,op.clip_bottom});s.text(program.text_at(op.text_slot),op.y,op.size,op.color);s.clip({0,0,466,466});break;
  }
 }
}
static void motion(){
 unsigned cases=0;
 for(unsigned count=2;count<=16;++count)for(unsigned selected=0;selected<count;++selected)for(int direction:{-1,1}){
  MotionPager pager;CarouselScene id{7,1,1,0,count,selected,5,6};CHECK(pager.update(id,0,true));CHECK(!pager.previewing());
  CHECK(pager.update(id,10,true,true,-direction*50));const float first=pager.position();CHECK(first*direction>0.4f);
  CHECK(pager.update(id,30,true,true,-direction*80));CHECK(pager.position()*direction>first*direction);
  CHECK(pager.update(id,50,true,true,-direction*20));CHECK(pager.position()*direction<first*direction);
  CHECK(pager.update(id,70,true,false));CHECK(pager.previewing());
  CHECK(pager.update(id,170,true,true,0));const float caught=pager.position();
  CHECK(pager.update(id,180,true,true,-direction*20));CHECK(std::fabs(pager.position()-caught-direction*14.0f/94)<0.00001f);
  CHECK(pager.update(id,200,true,false,0,1,direction));CHECK(pager.previewing());
  CHECK(pager.update(id,300,true,false,0,1,direction));const unsigned next=(selected+count+direction)%count;const auto before=pager.pose(next);
  id.selected=next;++id.selection_epoch;
  CHECK(pager.update(id,300,true,false,0,2,direction));const auto after=pager.pose(next);
  CHECK(before.x==after.x&&before.y==after.y&&before.scale==after.scale); // No ACK jump.
  CHECK(pager.update(id,501,true));CHECK(!pager.moving()&&!pager.previewing()&&pager.position()==0);
  for(int dx=-100;dx<=100;dx+=5){MotionPager p;CHECK(p.update(id,0,true));CHECK(p.update(id,10,true,true,dx));unsigned indices[4]{};const unsigned n=p.order(indices);CHECK(n>=2&&n<=4);for(unsigned j=0;j<n;++j){CHECK(indices[j]<count&&p.pose(indices[j]).alpha>0);if(j)CHECK(p.pose(indices[j-1]).depth<=p.pose(indices[j]).depth);for(unsigned k=0;k<j;++k)CHECK(indices[j]!=indices[k]);}}
  CHECK(pager.update(id,600,true,true,-70));CHECK(pager.previewing());CHECK(pager.update(id,601,false,true,-70));CHECK(!pager.previewing()&&pager.position()==0);
  CHECK(pager.update(id,700,true,true,-70));++id.roster;CHECK(pager.update(id,701,true));CHECK(pager.position()==0);
  CHECK(pager.update(id,800,true,true,-70));CHECK(pager.update(id,1,true));CHECK(pager.position()==0);
  ++cases;
 }
 // Every visible pose survives early/mid/late ACK rebasing, including wrap.
 for(unsigned count=2;count<=16;++count)for(unsigned selected=0;selected<count;++selected)for(int direction:{-1,1})for(int delay:{0,40,120,400}){
  MotionPager p;CarouselScene id{7,1,1,0,count,selected,5,6};CHECK(p.update(id,0,true));CHECK(p.update(id,10,true,true,-direction*70));CHECK(p.update(id,20,true,false,0,1,direction));CHECK(p.update(id,20+delay,true,false,0,1,direction));
  CarouselPose before[16];for(unsigned i=0;i<count;++i)before[i]=p.pose(i);unsigned old_order[4]{};const auto old_count=p.order(old_order);
  id.selected=(selected+count+direction)%count;++id.selection_epoch;CHECK(p.update(id,20+delay,true,false,0,2,direction));unsigned new_order[4]{};CHECK(p.order(new_order)==old_count);
  for(unsigned i=0;i<count;++i){const auto after=p.pose(i);CHECK(std::abs(before[i].x-after.x)<=1&&std::abs(before[i].y-after.y)<=1&&std::abs(before[i].scale-after.scale)<=1&&std::abs(static_cast<int>(before[i].alpha)-static_cast<int>(after.alpha))<=1);}
  for(unsigned i=1;i<old_count;++i)if(std::abs(before[old_order[i]].depth-before[old_order[i-1]].depth)>4)CHECK(old_order[i]==new_order[i]);
 }
 // A no-change ACK returns smoothly; unrelated selection must not borrow a
 // preview's neighbor provenance. Both remain display-only operations.
 MotionPager denied;CarouselScene id{7,1,1,0,5,0,5,6};CHECK(denied.update(id,0,true));CHECK(denied.update(id,10,true,true,-70));CHECK(denied.update(id,20,true,false,0,1,1));CHECK(denied.update(id,240,true,false,0,1,1));CHECK(denied.position()==1);
 CHECK(denied.update(id,250,true,false,0,2,1));CHECK(denied.position()==1&&denied.previewing());CHECK(denied.update(id,451,true));CHECK(denied.position()==0&&!denied.previewing());
 CHECK(denied.update(id,500,true,true,-70));id.selected=3;++id.selection_epoch;CHECK(denied.update(id,501,true,false,0,2,1));CHECK(denied.position()==0&&!denied.previewing());
 MotionPager invalid;CarouselScene bad{};CHECK(!invalid.update(bad,0,true));
 std::cout<<"motion: "<<cases<<" count/selection/direction cases, reversal, interruption, wrap, ACK continuity, reduced motion and identity resets\n";
}
int main(){
 motion();static SpriteRows sprites;CHECK(sprites.initialize()&&sprites.initialize());unsigned cases=0;uint64_t indexed=0,old_runs=0;
 for(unsigned state=0;state<9;++state)for(bool blink:{false,true})for(unsigned archetype=0;archetype<4;++archetype)for(unsigned scale:{32u,66u,180u,256u}){
  SceneProgram scene;const int x=cases%3==0?5:233,y=cases%5==0?455:215;
  CHECK(scene.ghost(static_cast<State>(state),blink,x,y,true,scale,(state*37)%360,archetype,cases%2?128:255));
  Surface expected,actual;reference(scene,expected);RetainedFrame frame;CHECK(frame.prepare(scene));CHECK(frame.paint(sprites,bg,actual));CHECK(expected.pixels==actual.pixels);CHECK(frame.commit());indexed+=frame.visited_runs();old_runs+=watcher_art::sprites[state][blink?1:0].count;++cases;
 }
 {Surface actual;RetainedFrame scrolling;
  for(int offset=0;offset<=144;++offset){SceneProgram scene;CHECK(settings_scene(scene,{15,240},0,watcher_settings::Save::unsaved,true,true,true,offset));Surface expected;reference(scene,expected);CHECK(scrolling.prepare(scene)&&scrolling.paint(sprites,bg,actual)&&scrolling.commit());CHECK(actual.pixels==expected.pixels);}
  SceneProgram narrower;CHECK(narrower.text("clipped",110,2,0xef7d,0x0842,120,130));CHECK(narrower.text("footer",414,2,0xef7d));Surface expected;reference(narrower,expected);CHECK(scrolling.prepare(narrower)&&scrolling.paint(sprites,bg,actual)&&scrolling.commit());CHECK(actual.pixels==expected.pixels);
 }
 CHECK(indexed<old_runs); // Work reduction, NOT a hardware timing assertion.
 for(unsigned state=0;state<9;++state){SceneProgram scene;CHECK(scene.ghost(static_cast<State>(state),false,233,215));Surface expected,actual;reference(scene,expected);RetainedFrame frame;CHECK(frame.prepare(scene)&&frame.paint(sprites,bg,actual)&&frame.commit());CHECK(expected.pixels==actual.pixels);}
 Surface actual;RetainedFrame retained;unsigned partial=0,unchanged=0;
 for(unsigned step=0;step<160;++step){
  SceneProgram scene;CHECK(scene.shadow());
  const unsigned layers=step%17==0?2:4;
  for(unsigned j=0;j<layers;++j){const int x=100+j*80+(j==0?step%23:0),y=210+j*15;CHECK(scene.ghost(static_cast<State>((step/13+j)%9),step%19==0,x,y,true,66+j*32,j*90,j,255-j*25));CHECK(scene.circle(x+20,y-20,6,0xf672,false));}
  CHECK(scene.text(step%29==0?"Changed title":"Stable title",53,2.5f,0xef7d));CHECK(scene.text("Occluding footer",340,3,0xef7d));
  Surface expected;reference(scene,expected);CHECK(retained.prepare(scene));if(retained.dirty().area()<466*466)++partial;
  CHECK(retained.paint(sprites,bg,actual));CHECK(actual.pixels==expected.pixels);CHECK(retained.commit());
  const auto clears=actual.clears;CHECK(retained.prepare(scene)&&retained.dirty().empty());CHECK(retained.paint(sprites,bg,actual)&&retained.commit());CHECK(clears==actual.clears);++unchanged;
  if(step%31==0)retained.reset();
 }
 CHECK(partial>80&&unchanged==160);
 SceneProgram empty;CHECK(!retained.prepare(empty));SceneProgram good;CHECK(good.ghost(State::idle,false,233,215));retained.reset();CHECK(retained.prepare(good));CHECK(!retained.prepare(good));Surface throws;throws.throws=true;bool caught=false;try{retained.paint(sprites,bg,throws);}catch(const std::runtime_error&){caught=true;}CHECK(caught&&!retained.commit()&&!retained.paint(sprites,bg,throws)&&!retained.prepare(good));
 std::cout<<"retained: "<<cases<<" indexed raster comparisons; 160 retained/full model-pixel comparisons; 160 no-op frames; indexed visits "<<indexed<<" vs full traversal "<<old_runs<<"\n";
}
