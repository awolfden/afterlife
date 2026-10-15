// Included inside main's namespace. Preparation is synchronous RAM-only work;
// all strings/poses become owned commands before returning to the outer loop.
#ifdef WATCHER_GHOST_FAMILY
// Last actually published costume may remain under the Disconnected label.
// It carries no stale row, title, liveness, selected identity or voice authority.
static int last_published_character=-1;
#endif
static bool prepare_bounded_scene(int64_t now,watcher_render::SceneProgram& program,watcher_render::RenderSnapshot& snapshot){
 program=watcher_render::SceneProgram{};
 const bool linked=frame.seq&&now>=last_frame_ms&&now-last_frame_ms<=1500;
 const auto page_mode=ghost_page.mode(frame,now);
 const bool group=linked&&(page_mode==watcher_ghost::Mode::group||page_mode==watcher_ghost::Mode::carousel);
#ifdef WATCHER_TOUCH_TIMING_DIAGNOSTICS
 touch_timing.metadata(linked&&page_mode==watcher_ghost::Mode::waiting,ms());
#endif
#ifdef WATCHER_AFTERLIFE
 if(desk.open()){
  focus_transition.reset();carousel.reset();
  snapshot={bounded_identity(now),linked?frame.seq:0,0,0,false,true};
  return watcher_render::settings_scene(program,desk.sync().values(),desk.top(),desk.sync().save(),desk.sync().forced(),frame.flags&1,linked&&!desk.sync().supported(),desk.scroll_offset(frame.flags&8));
 }
#endif
 if(linked&&page_mode==watcher_ghost::Mode::waiting)return false;
 const auto* focused=group?ghost_page.get(frame.selected,frame):nullptr;
 program.family_selection=focused&&focused->family?static_cast<int8_t>(focused->character):-1;
 snapshot={bounded_identity(now),linked?frame.seq:0,paging_event,frame.event_ack,false};
 uint32_t scene_paging_event=paging_event;
 unsigned shown_mask=0,shown_count=0;
 const unsigned state=linked?frame.state:(ever_linked?8:9),art_state=state==9?8:state;
 if(state>9||frame.count>16)return false;
#ifdef WATCHER_AFTERLIFE
 const bool connecting=linked&&state==9&&frame.count==0;
 if(!linked||connecting){
  // Fresh host with no registered sessions is waiting, not a selected ghost.
  // Branding/loader never renew liveness or confer session/voice authority.
  focus_transition.reset();carousel.reset();
  if(!program.afterlife())return false;
  if(!connecting)return program.text("Connect to Pi",340,2.5f,0xe71e,0x1063)&&watcher_render::settings_entry(program,true);
  const auto pose=watcher_render::connecting_pose(now,!desk.animations(frame.flags&8));
  char prompt[]="Connecting to Pi   ";
  for(unsigned i=0;i<pose.dots;++i)prompt[16+i]='.';
  // Fixed-width native text keeps the label anchored as dots cycle.
  // Font0 treats equal foreground/background as transparent. The longer
  // prompt must not erase the mural beneath its outer glyph cells.
  if(!program.text(prompt,340,2.5f,0xe71e,0xe71e))return false;
  // Reserve the entry hit region from y=406. A half-size loader at y=381
  // stays below the prompt and above that region even at both bob extremes.
  return program.family_ghost(watcher_render::State::idle,pose.blink,pose.x,381+(pose.y-395)/4,0,32)&&watcher_render::settings_entry(program,true);
 }
#endif
#ifdef WATCHER_AFTERLIFE
 const bool reduced=!desk.animations(frame.flags&8),animated=linked&&state<7&&!reduced;
#else
 const bool reduced=frame.flags&8,animated=linked&&state<7&&!reduced;
#endif
 const float t=now/1000.0f,speed=state==2?1.15f:state==1?.64f:.42f;
 const int x=233+(animated?static_cast<int>(sinf(t*speed)*(state==2?21:state>=3?4:12)):0);
 int y=215+(animated?static_cast<int>(sinf(t*speed*1.37f)*(state==2?7:5)):0);
 if(animated&&state==6)y=215-static_cast<int>(powf(fmaxf(0,sinf(t*1.3f)),4)*5);
 if(focused&&watcher_render::multi_session_layout(frame.count)){
  unsigned indices[4]{},visible=0;watcher_render::CarouselPose positions[4]{};
  watcher_render::FocusScene scene;scene.boot=frame.boot;scene.count=frame.count;scene.selected=frame.selected;scene.rotation=rotation;
  scene.source=static_cast<unsigned>(link_source);scene.generation=link_generation;
  const bool identity_ready=focus_context.matches(frame);
  if(identity_ready){scene.roster=focus_context.roster;scene.selection_epoch=focus_context.selection;}
  bool move_focus=!reduced;
  const bool dragging=identity_ready&&swipe.tracking();const int displacement=swipe.displacement();
  const unsigned release=paging_event?(frame.event_ack>=paging_event?2u:1u):0u;
  if(page_mode==watcher_ghost::Mode::carousel&&identity_ready){
   focus_transition.reset();
   const watcher_render::CarouselScene ring{scene.boot,scene.generation,scene.source,scene.rotation,scene.count,scene.selected,scene.roster,scene.selection_epoch};
   if(!carousel.update(ring,now,move_focus,dragging,displacement,release,paging_direction))return false;
   visible=carousel.order(indices);if(!visible||visible>4){carousel.reset();return false;}
   for(unsigned n=0;n<visible;++n)positions[n]=carousel.pose(indices[n]);
  }else{
   carousel.reset();const unsigned start=frame.selected/4*4;visible=(frame.count-start)<4?frame.count-start:4;
   for(unsigned slot=0;slot<visible;++slot){const auto* row=ghost_page.get(start+slot,frame);if(!row){focus_transition.reset();return false;}scene.hue[slot]=row->hue;scene.rhythm[slot]=row->rhythm;}
   if(!focus_transition.update(scene,now,move_focus,dragging,displacement,release))return false;
   unsigned order[4];focus_transition.order(order);
   for(unsigned n=0;n<visible;++n){indices[n]=start+order[n];const auto p=focus_transition.pose(order[n]);positions[n]={p.x,p.y,p.scale,0,255};}
  }
  for(unsigned n=0;n<visible;++n)if(!ghost_page.get(indices[n],frame)){focus_transition.reset();carousel.reset();return false;}
  // Original synchronous render cleared here. Keep the UI decision, but defer
  // the actual event cleanup until THIS snapshot has finished transferring.
  if(release==2)scene_paging_event=0;
  for(unsigned n=0;n<visible;++n){
   const unsigned i=indices[n];const auto* row=ghost_page.get(i,frame);const auto position=positions[n];
   shown_mask|=1u<<i;++shown_count;
   if(i==frame.selected){
    const auto pose=watcher_render::personality_pose(static_cast<watcher_render::State>(art_state),reduced,now,focused->rhythm,false);
    if(focused->family)program.family_ghost(static_cast<watcher_render::State>(art_state),pose.blink,position.x+pose.x,position.y+pose.y,focused->character,position.scale,position.alpha);
    else program.ghost(static_cast<watcher_render::State>(art_state),pose.blink,position.x+pose.x,position.y+pose.y,true,position.scale,focused->hue,focused->rhythm,position.alpha);
   }else{
    const auto pose=watcher_render::personality_pose(static_cast<watcher_render::State>(row->state),reduced,now,row->rhythm,true);
    const int xx=position.x+pose.x,yy=position.y+pose.y;
    if(row->family)program.family_ghost(static_cast<watcher_render::State>(row->state),pose.blink,xx,yy,row->character,position.scale,position.alpha);
    else program.ghost(static_cast<watcher_render::State>(row->state),pose.blink,xx,yy,true,position.scale,row->hue,row->rhythm,position.alpha);
    if(row->pending){const int bx=xx+25*position.scale/66,by=yy-23*position.scale/66;const auto color=watcher_render::carousel_color(0xf672,background,position.alpha);program.circle(bx,by,6,color,false);if(row->unseen)program.circle(bx,by,3,color,true);}
   }
  }
 }else{
  focus_transition.reset();carousel.reset();if(focused){shown_mask=1u<<frame.selected;shown_count=1;}
  program.shadow();
  if(focused&&focused->family)program.family_ghost(static_cast<watcher_render::State>(art_state),animated&&now%5700>5520,x,y,focused->character);
#ifdef WATCHER_GHOST_FAMILY
  else if(!linked&&ever_linked&&last_published_character>=0)program.family_ghost(static_cast<watcher_render::State>(art_state),false,x,y,static_cast<unsigned>(last_published_character));
#endif
  else program.ghost(static_cast<watcher_render::State>(art_state),animated&&now%5700>5520,x,y);
 }
 const char* labels[]={"All quiet","Thinking","Working","Needs you","Child needs help","All done","Failed","Status unknown","Disconnected","Connect to Pi"};
 char line[64];
 if(focused)snprintf(line,sizeof(line),"%s (%lu)",focused->title,(unsigned long)focused->space_count);
 else if(frame.count)snprintf(line,sizeof(line),"Session %lu",(unsigned long)frame.selected+1);else snprintf(line,sizeof(line),"Pi watcher");
 program.text(line,53,strlen(line)>24?1.6f:2.5f,0xef7d);
 for(unsigned i=0;i<frame.count;++i)program.circle(233+(static_cast<int>(i)-(static_cast<int>(frame.count)-1)/2)*12,91,i==frame.selected?4:2,0xb43a,true);
 program.text(labels[state],340,state==4?2.5f:3.0f,state>=3&&state<=6&&state!=5?0xf672:0xef7d);
 const char* detail="Nothing needs you yet";
 if(state==3)detail=(frame.flags&2)?"Acknowledged; still waiting":"Waiting for your input";
 else if(state==4){snprintf(line,sizeof(line),"%lu pending request%s",(unsigned long)frame.pending,frame.pending==1?"":"s");detail=(frame.flags&2)?"Acknowledged; still waiting":line;}
 else if(state==5)detail="Session complete";
 else if(state==6)detail=(frame.flags&2)?"Acknowledged; failure retained":"Check this session";
 else if(state==7)detail="Waiting for fresh status";
 else if(state==8)detail=(frame.flags&16)?"Request retained":"Reconnect to your Mac";
 else if(state==9)detail="Start the USB host bridge";
#ifndef WATCHER_AFTERLIFE
 if((frame.flags&4)&&state<7)detail="Source status incomplete";
#endif
 snapshot.preview=focus_transition.previewing()||carousel.previewing();
 if(snapshot.preview)detail=scene_paging_event?"Waiting for selection":"Preview; release to switch";
 program.text(detail,381,1.5f,0xbdd7);
 const char* feedback_label="Haptics off";
#if defined(WATCHER_HAPTICS) && !defined(WATCHER_FACTORY_ACCEPTANCE)
 feedback_label=audio.ready()?"Haptics on":"Feedback fault";
#endif
 if(frame.flags&1)feedback_label="Muted";
 if(focused){
  unsigned local_waiting=0;for(unsigned i=0;i<frame.count;++i)if(shown_mask&(1u<<i)){const auto* row=ghost_page.get(i,frame);if(i!=frame.selected&&row&&row->pending)++local_waiting;}
  const unsigned hidden_waiting=frame.other>local_waiting?frame.other-local_waiting:0;
  snprintf(line,sizeof(line),"%lu offscreen / %u waiting / %s",(unsigned long)(frame.count-shown_count),hidden_waiting,feedback_label);
 }else if(frame.other)snprintf(line,sizeof(line),"%lu elsewhere / %s",(unsigned long)frame.other,feedback_label);
 else snprintf(line,sizeof(line),"%lu / %lu / %s",(unsigned long)(frame.count?frame.selected+1:0),(unsigned long)frame.count,feedback_label);
#ifdef WATCHER_AFTERLIFE
 if(!watcher_render::settings_entry(program))return false;
#else
 program.text(line,410,1.4f,0xa514);
#endif
#if defined(WATCHER_TOUCH_DIAGNOSTICS) && !defined(WATCHER_TOUCH_TIMING_DIAGNOSTICS)
 bool show_touch=true;
 if(show_touch){char top[48],bottom[48];watcher_touch::diagnostic_lines(top,bottom,touch_sampler,swipe,touch_eligible,touch_blocked);program.text(top,427,1,0xbdd7);program.text(bottom,443,1,0xbdd7);}
#endif
 return program.valid();
}
