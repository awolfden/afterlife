#include "../firmware/render/settings-scene.h"
#include "../firmware/render/retained-frame.h"
#include "../firmware/pet/main/desk-runtime.h"
#include "../firmware/pet/main/status-route.h"
#include "../firmware/pet/components/watcher_lan/packet-queue.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <utility>
using namespace watcher_settings;
static InputEvent tap(Input& input,InputContext c,int x,int y,int64_t at=0){
 input.update({watcher_touch::Kind::released},c,at);input.update({watcher_touch::Kind::released},c,at+20);
 auto event=input.update({watcher_touch::Kind::contact,x,y},c,at+40);assert(event.action==Action::none);
 input.update({watcher_touch::Kind::released},c,at+60);return input.update({watcher_touch::Kind::released},c,at+80);
}
int main(){
 InputContext closed{true,false,1,0,0},open{true,true,2,0,0};
 {Input input;assert(tap(input,closed,116,213).action==Action::open);}
 {Input input;assert(tap(input,open,116,213).action==Action::back);}
 for(unsigned top=0;top<3;++top)for(unsigned row=0;row<3;++row){Input input;auto c=open;c.top=top;const auto e=tap(input,c,116,70+row*36);assert(e.action==Action::toggle&&e.row==top+row&&e.claimed);}
 {Input input;const auto e=tap(input,closed,116,110);assert(e.action==Action::none&&!e.claimed);}
 {Input input;input.update({watcher_touch::Kind::released},open,0);input.update({watcher_touch::Kind::released},open,20);
  assert(input.update({watcher_touch::Kind::contact,116,142},open,40).claimed);input.update({watcher_touch::Kind::contact,116,110},open,60);
  input.update({watcher_touch::Kind::released},open,80);const auto e=input.update({watcher_touch::Kind::released},open,100);assert(e.action==Action::scroll&&e.direction==1&&e.claimed);
 }
 {Input input;input.update({watcher_touch::Kind::released},open,0);input.update({watcher_touch::Kind::released},open,20);
  input.update({watcher_touch::Kind::contact,116,142},open,40);input.update({watcher_touch::Kind::released},open,60);
  input.update({watcher_touch::Kind::contact,116,110},open,70);input.update({watcher_touch::Kind::released},open,90);
  const auto bounced=input.update({watcher_touch::Kind::released},open,110);assert(bounced.action==Action::none&&bounced.claimed);
 }
 for(unsigned fault=0;fault<4;++fault){
  Input input;auto c=open;input.update({watcher_touch::Kind::released},c,0);input.update({watcher_touch::Kind::released},c,20);input.update({watcher_touch::Kind::contact,116,70},c,40);
  if(fault==0)++c.generation;if(fault==1)c.rotation=1;if(fault==2)c.enabled=false;
  const auto e=input.update({fault==3?watcher_touch::Kind::error:watcher_touch::Kind::released},c,60);assert(e.action==Action::none);
  assert(input.update({watcher_touch::Kind::released},c,80).action==Action::none);
 }
 // Runtime glue: optimistic disabling is immediate, enabling requires host echo.
 {Runtime runtime;Runtime::Emission scratch;std::string tail;const auto send=[&](){return runtime.status_batch("D2 test\n",8,scratch,[&](const char* bytes,size_t size){tail.assign(bytes,size);return true;});};runtime.context(7,10,0,true);assert(runtime.outputs()==0);
  const auto echo=[&](unsigned revision,unsigned ack,unsigned flags){std::string payload="S1 7 10 11111111111111111111111111111111 "+std::to_string(revision)+" "+std::to_string(ack)+" 0 "+std::to_string(flags)+" 160 0 ";payload+=std::to_string(watcher_wire::crc(payload.data(),payload.size()));return runtime.receive(payload.data(),payload.size());};
  assert(echo(1,0,5)&&runtime.outputs()==2);assert(runtime.change(0)&&runtime.outputs()==0);
  assert(!runtime.status_batch("D2 test\n",8,scratch,[](const char*,size_t){return false;}));
  runtime.context(7,10,1501,true);assert(runtime.sync().save()==Save::saving);assert(send());assert(std::string(tail).find("S2 7 10 ")!=std::string::npos);
  assert(echo(2,1,4)&&runtime.outputs()==0);runtime.context(7,10,1751,true);assert(runtime.change(0)&&runtime.outputs()==0);assert(send());assert(echo(3,2,5)&&runtime.outputs()==2);
  runtime.context(7,10,1800,false);assert(runtime.outputs()==0);assert(runtime.change(4)&&runtime.sync().values().brightness==240);
 }
 {Runtime runtime;const auto sample=[&](int64_t at,watcher_touch::Kind kind){runtime.context(7,0,at,false);return runtime.touch({kind,116,213},true,0);};
  sample(0,watcher_touch::Kind::released);sample(20,watcher_touch::Kind::released);sample(40,watcher_touch::Kind::contact);sample(60,watcher_touch::Kind::released);
  assert(sample(80,watcher_touch::Kind::released).action==Action::open&&runtime.open());assert(runtime.activity()&1);
  runtime.context(7,0,120,false);assert(runtime.back());assert(runtime.activity()==4);runtime.context(7,0,1119,false);assert(runtime.activity()==4);runtime.context(7,0,1120,false);assert(runtime.activity()==0);
 }
 {watcher_lan::PacketQueue queue;watcher_lan::Packet packet;packet.size=2;std::memcpy(packet.bytes,"x\n",2);
  for(unsigned i=0;i<13;++i)assert(queue.push(packet));
  const std::string batch="D2 test\nS0 test\nS2 test\nS3 test\n";
  assert(!queue.batch(7,100,batch.data(),batch.size())&&queue.count==13);
  assert(queue.pop(packet)&&queue.pop(packet));assert(queue.batch(7,100,batch.data(),batch.size())&&queue.count==15);
  for(unsigned i=0;i<11;++i){assert(queue.pop(packet));assert(packet.bytes[0]=='x');}
  std::string received;for(unsigned i=0;i<4;++i){assert(queue.pop(packet));assert(packet.generation==7&&packet.at==100);received.append(packet.bytes,packet.size);}assert(received==batch&&queue.count==0);
  for(const auto& bad:{std::string("missing newline"),std::string("\n"),std::string(192,'x')+"\n",std::string("x\nx\nx\nx\nx\n")})assert(!queue.batch(7,100,bad.data(),bad.size())&&queue.count==0);
  const std::string line=std::string(191,'x')+"\n",maximal=line+line+line+line;assert(queue.batch(7,100,maximal.data(),maximal.size()));queue.clear();assert(queue.count==0);
 }
 // Discovery reaches LAN even when USB succeeds; established routes never fan out.
 {enum class Source {none,usb,lan};
  for(auto source:{Source::none,Source::usb,Source::lan})for(bool usb_ok:{false,true})for(bool lan_ok:{false,true})for(unsigned connected:{0u,9u}){
   unsigned usb_calls=0,lan_calls=0,used_generation=0;
   const auto usb=[&](const char* bytes,size_t size){++usb_calls;assert(std::string(bytes,size)=="D2 test\n");return usb_ok;};
   const auto lan=[&](uint32_t gen,const char* bytes,size_t size){++lan_calls;used_generation=gen;assert(std::string(bytes,size)=="D2 test\n");return lan_ok;};
   const bool admitted=status_route(source,7,connected,"D2 test\n",8,usb,lan);
   assert(usb_calls==(source==Source::lan?0u:1u));assert(lan_calls==(source==Source::lan||(source==Source::none&&connected)?1u:0u));
   assert(admitted==(source==Source::lan?lan_ok:source==Source::usb?usb_ok:usb_ok||(connected&&lan_ok)));
   if(lan_calls)assert(used_generation==(source==Source::lan?7u:connected));
  }
  assert(!status_route(Source::lan,0,9,"D2 test\n",8,[](const char*,size_t){assert(false);return true;},[](uint32_t,const char*,size_t){assert(false);return true;}));
  for(int y=387;y<=403;++y){watcher_render::SceneProgram p;assert(p.family_ghost(watcher_render::State::idle,false,233,381+(y-395)/4,0,32));const auto bound=watcher_render::paint_bounds(p.op(0));assert(bound.y>=360&&bound.bottom<=entry_rect.y);}
  Runtime discover;Runtime::Emission scratch;discover.context(7,0,0,false);
  bool reached=false;assert(discover.status_batch("D2 test\n",8,scratch,[&](const char* bytes,size_t size){return status_route(Source::none,0,9,bytes,size,[](const char*,size_t){return false;},[&](uint32_t gen,const char* data,size_t length){reached=true;assert(gen==9&&std::string(data,length)=="D2 test\n");return true;});}));assert(reached&&!discover.sync().supported());
 }
 // Runtime integration: intermediate pixels never change logical generation;
 // release commits one row without a pose jump, and settling cannot toggle.
 {Runtime runtime;const auto sample=[&](int64_t at,watcher_touch::Kind kind,int y){runtime.context(7,0,at,false);return runtime.touch({kind,116,y},true,0);};
  sample(0,watcher_touch::Kind::released,213);sample(20,watcher_touch::Kind::released,213);sample(40,watcher_touch::Kind::contact,213);sample(60,watcher_touch::Kind::released,213);assert(sample(80,watcher_touch::Kind::released,213).action==Action::open);
  sample(100,watcher_touch::Kind::released,142);sample(120,watcher_touch::Kind::released,142);const auto generation=runtime.generation();const auto values=runtime.sync().values();
  sample(140,watcher_touch::Kind::contact,142);sample(160,watcher_touch::Kind::contact,122);assert(runtime.scroll_offset(false)==40&&runtime.top()==0&&runtime.scrolling()&&runtime.generation()==generation);
  sample(180,watcher_touch::Kind::released,122);assert(runtime.scroll_offset(false)==40);assert(sample(200,watcher_touch::Kind::released,122).action==Action::scroll);assert(runtime.top()==1&&runtime.scroll_offset(false)==40&&runtime.scrolling());
  assert(sample(220,watcher_touch::Kind::contact,70).action==Action::none);sample(240,watcher_touch::Kind::released,70);sample(260,watcher_touch::Kind::released,70);assert(runtime.sync().values()==values);
  runtime.context(7,0,400,false);assert(runtime.scroll_offset(false)==72&&!runtime.scrolling());
  sample(420,watcher_touch::Kind::released,142);sample(440,watcher_touch::Kind::released,142);sample(460,watcher_touch::Kind::contact,142);sample(480,watcher_touch::Kind::contact,122);assert(runtime.scroll_offset(false)==112);assert(runtime.scroll_offset(true)==72);
  runtime.cancel();assert(runtime.scroll_offset(false)==72&&!runtime.scrolling());
  sample(500,watcher_touch::Kind::released,142);sample(520,watcher_touch::Kind::released,142);sample(540,watcher_touch::Kind::contact,142);sample(560,watcher_touch::Kind::contact,122);assert(runtime.scroll_offset(false)==112);
  runtime.context(7,10,580,true);std::string echo="S1 7 10 11111111111111111111111111111111 1 0 0 5 160 0 ";echo+=std::to_string(watcher_wire::crc(echo.data(),echo.size()));assert(runtime.receive(echo.data(),echo.size()));assert(runtime.scroll_offset(false)==72&&!runtime.scrolling());assert(runtime.back()&&!runtime.open()&&!runtime.scrolling());
 }
 for(const auto bounds:{std::pair<unsigned,unsigned>{120,120},{121,120},{0,467}}){watcher_render::SceneProgram p;assert(!p.text("bad clip",120,2,0xef7d,0x0842,bounds.first,bounds.second)&&!p.valid());}
 // Presentation follows contact without changing the logical row, then eases
 // monotonically over the same 200ms interval as horizontal paging.
 {Scroll scroll;assert(scroll.update(0,0,false,0,true)==0);assert(scroll.update(20,0,true,-40,true)==40);assert(scroll.moving());
  assert(scroll.update(40,1,false,0,true)==40);const int middle=scroll.update(140,1,false,0,true);assert(middle>40&&middle<72);assert(scroll.update(240,1,false,0,true)==72&&!scroll.moving());
  assert(scroll.update(260,1,true,200,true)==0);assert(scroll.update(280,1,false,0,false)==72&&!scroll.moving());
  assert(scroll.update(300,1,true,-200,true)==144);assert(scroll.update(290,1,false,0,true)==72&&!scroll.moving());
 }
 {Input input;input.update({watcher_touch::Kind::released},open,0);input.update({watcher_touch::Kind::released},open,20);
  input.update({watcher_touch::Kind::contact,116,142},open,40);input.update({watcher_touch::Kind::contact,116,122},open,60);assert(input.dragging()&&input.displacement()==-40);
  input.update({watcher_touch::Kind::contact,116,142},open,80);assert(input.dragging());input.update({watcher_touch::Kind::released},open,100);assert(input.update({watcher_touch::Kind::released},open,120).action==Action::none);
 }
 {Input input;input.update({watcher_touch::Kind::released},open,0);input.update({watcher_touch::Kind::released},open,20);
  input.update({watcher_touch::Kind::contact,116,213},open,40);input.update({watcher_touch::Kind::contact,116,100},open,60);assert(!input.dragging());input.update({watcher_touch::Kind::released},open,80);assert(input.update({watcher_touch::Kind::released},open,100).action==Action::none);
 }
 for(int offset=0;offset<=144;++offset){watcher_render::SceneProgram p;assert(watcher_render::settings_scene(p,{15,240},0,Save::unsaved,true,true,true,offset));assert(p.count()<=8);
  for(unsigned i=0;i<p.count();++i){const auto& op=p.op(i);if(op.clip_top){assert(op.clip_top==120&&op.clip_bottom==336);const auto bounds=watcher_render::paint_bounds(op);assert(bounds.y>=120&&bounds.bottom<=336);}}
  watcher_render::SceneJob compatibility;assert(!compatibility.begin(p));
 }
 unsigned scenes=0;
 for(unsigned top=0;top<3;++top)for(unsigned flags=0;flags<16;++flags)for(unsigned bright:{80u,160u,240u})for(auto save:{Save::temporary,Save::saving,Save::saved,Save::unsaved})for(unsigned notes=0;notes<8;++notes){
  watcher_render::SceneProgram p;assert(watcher_render::settings_scene(p,{flags,bright},top,save,notes&1,notes&2,notes&4));assert(p.count()<=7);
  for(unsigned i=0;i<p.count();++i){const auto& op=p.op(i);assert(op.kind==watcher_render::SceneKind::text);const auto* text=p.text_at(op.text_slot);
   // Conservative Font0 cell bounds (6x8). Native glyph pixels still need the pinned vendor witness.
   const double half=std::strlen(text)*6*op.size/2,dy=std::max(std::abs(op.y-233.0),std::abs(op.y+8*op.size-233.0));assert(half*half+dy*dy<=232.0*232.0);
  }++scenes;
 }
 for(const auto r:{entry_rect,row_rect(0),row_rect(1),row_rect(2)})for(int x:{r.x,r.x+r.w-1})for(int y:{r.y,r.y+r.h-1})assert((x-233)*(x-233)+(y-233)*(y-233)<=232*232);
 std::cout<<scenes<<" settings scenes: bounded text, release taps, scrolling and stale-view rejection pass\n";
}
