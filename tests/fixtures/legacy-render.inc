// Unmodified render() reference from pre-integration main.cpp.
// Full original SHA256 40963db64085802b351a24ca9d0653714671074940ab6c9ee9bbbe0875501de5
static void render(int64_t now){
 const bool linked=frame.seq && now-last_frame_ms<=1500;
#ifdef WATCHER_GHOST_PAGES
 const auto page_mode=ghost_page.mode(frame,now);
 const bool group=linked&&(page_mode==watcher_ghost::Mode::group||page_mode==watcher_ghost::Mode::carousel);
 unsigned shown_mask=0,shown_count=0;
#ifdef WATCHER_TOUCH_TIMING_DIAGNOSTICS
 touch_timing.metadata(linked&&page_mode==watcher_ghost::Mode::waiting,ms());
#endif
 if(linked&&page_mode==watcher_ghost::Mode::waiting)return; // Bounded hold, retaining the exact rendered ACK identity.
 const auto* focused=group?ghost_page.get(frame.selected,frame):nullptr;
#endif
 const unsigned state=linked?frame.state:(ever_linked?8:9);
 const unsigned art_state=state==9?8:state;
 const bool reduced=frame.flags&8;
 const float t=now/1000.0f,speed=state==2?1.15f:state==1?.64f:.42f;
 const bool animated=linked && state<7 && !reduced;
 const int x=233+(animated?static_cast<int>(sinf(t*speed)*(state==2?21:state>=3?4:12)):0);
 int y=215+(animated?static_cast<int>(sinf(t*speed*1.37f)*(state==2?7:5)):0);
 if(animated && state==6)y=215-static_cast<int>(powf(fmaxf(0,sinf(t*1.3f)),4)*5);
 canvas.fillScreen(background);
#ifdef WATCHER_GHOST_PAGES
 if(focused&&watcher_render::multi_session_layout(frame.count)){
  unsigned indices[4]{},visible=0;
  watcher_render::CarouselPose positions[4]{};
  watcher_render::FocusScene scene;scene.boot=frame.boot;scene.count=frame.count;scene.selected=frame.selected;
#if defined(WATCHER_AUTO_ROTATE) || defined(WATCHER_TOUCH_SWIPE)
  scene.rotation=rotation;
#endif
#ifdef WATCHER_LAN
  scene.source=static_cast<unsigned>(link_source);scene.generation=link_generation;
#endif
  const bool identity_ready=focus_context.matches(frame);
  if(identity_ready){scene.roster=focus_context.roster;scene.selection_epoch=focus_context.selection;}
  bool move_focus=!reduced;
#ifdef WATCHER_MIC_CAPTURE
  move_focus=move_focus&&!voice.consuming()&&!voice.busy();
#endif
  bool dragging=false;int displacement=0;
#ifdef WATCHER_TOUCH_SWIPE
  dragging=identity_ready&&swipe.tracking();displacement=swipe.displacement();
#endif
  const unsigned release=paging_event?(frame.event_ack>=paging_event?2u:1u):0u;
  if(page_mode==watcher_ghost::Mode::carousel&&identity_ready){
   focus_transition.reset();
   const watcher_render::CarouselScene ring{scene.boot,scene.generation,scene.source,scene.rotation,scene.count,scene.selected,scene.roster,scene.selection_epoch};
   if(!carousel.update(ring,now,move_focus,dragging,displacement,release,paging_direction))return;
   visible=carousel.order(indices);
   if(!visible){carousel.reset();return;}
   for(unsigned n=0;n<visible;n++)positions[n]=carousel.pose(indices[n]);
  }else{
   // Older hosts without the neighbor window retain the previous page layout.
   carousel.reset();
   const unsigned start=frame.selected/4*4;
   visible=(frame.count-start)<4?frame.count-start:4;
   for(unsigned slot=0;slot<visible;slot++){
    const auto* row=ghost_page.get(start+slot,frame);
    if(!row){focus_transition.reset();return;}
    scene.hue[slot]=row->hue;scene.rhythm[slot]=row->rhythm;
   }
   if(!focus_transition.update(scene,now,move_focus,dragging,displacement,release))return;
   unsigned order[4];focus_transition.order(order);
   for(unsigned n=0;n<visible;n++){
    indices[n]=start+order[n];const auto p=focus_transition.pose(order[n]);
    positions[n]={p.x,p.y,p.scale,0,255};
   }
  }
  for(unsigned n=0;n<visible;n++)if(!ghost_page.get(indices[n],frame)){focus_transition.reset();carousel.reset();return;}
  if(release==2)paging_event=0;
  for(unsigned n=0;n<visible;n++){
   const unsigned i=indices[n];
   const auto* row=ghost_page.get(i,frame);
   if(!row){focus_transition.reset();carousel.reset();return;}
   const auto position=positions[n];
   shown_mask|=1u<<i;++shown_count;
   const auto sink=[&](int xx,int yy,int length,uint16_t color){canvas.fillRect(xx,yy,length,1,watcher_render::carousel_color(color,background,position.alpha));};
   if(i==frame.selected){
    const auto pose=watcher_render::personality_pose(static_cast<watcher_render::State>(art_state),reduced,now,focused->rhythm,false);
    watcher_render::draw_personality(static_cast<watcher_render::State>(art_state),pose.blink,position.x+pose.x,position.y+pose.y,position.scale,focused->hue,focused->rhythm,sink);
   }else{
    const auto companion=watcher_render::personality_pose(static_cast<watcher_render::State>(row->state),reduced,now,row->rhythm,true);
    const int xx=position.x+companion.x,yy=position.y+companion.y;
    watcher_render::draw_personality(static_cast<watcher_render::State>(row->state),companion.blink,xx,yy,position.scale,row->hue,row->rhythm,sink);
    if(row->pending){const int badge_x=xx+25*position.scale/66,badge_y=yy-23*position.scale/66;const auto color=watcher_render::carousel_color(0xf672,background,position.alpha);canvas.drawCircle(badge_x,badge_y,6,color);if(row->unseen)canvas.fillCircle(badge_x,badge_y,3,color);}
   }
  }
 }else
#endif
 {
#ifdef WATCHER_GHOST_PAGES
  focus_transition.reset();carousel.reset();
  if(focused){shown_mask=1u<<frame.selected;shown_count=1;}
#endif
  canvas.fillEllipse(233,320,60,9,(uint16_t)0x28e7);
  watcher_render::draw_ghost(static_cast<watcher_render::State>(art_state),animated && now%5700>5520,x,y,[](int xx,int yy,int n,uint16_t color){canvas.fillRect(xx,yy,n,1,color);});
 }
#ifdef WATCHER_SUPERVISED_MOTOR_TEST
 text("Supervised motor test",340,2.0f,0xef7d);
 if(!probe_allowed){text("Test locked",381,2.0f,0xf672);text("Inspect PMIC revision/settings",410,1.3f,0xbdd7);}
 else if(probe_done){text(probe_ok?"Off readback OK":"POWER OFF NOW",381,2.0f,probe_ok?0xef7d:0xf672);text("Hold A 4s: return to init()",410,1.3f,0xbdd7);}
 else if(probe_gesture.phase==watcher_probe::Gesture::Phase::armed || probe_gesture.phase==watcher_probe::Gesture::Phase::pressed){text("Tap A once to test",381,2.0f,0xf672);text("80 ms / 25% / no repeat",410,1.3f,0xbdd7);}
 else {text("Hold A+B for 2 seconds",381,1.6f,0xef7d);text("Release both, then tap A",410,1.3f,0xbdd7);}
 canvas.pushSprite(0,0);displayed_seq=0;return;
#endif
 const char* labels[]={"All quiet","Thinking","Working","Needs you","Child needs help","All done","Failed","Status unknown","Disconnected","Connect to Pi"};
 char line[64];
#ifdef WATCHER_GHOST_PAGES
 if(focused)snprintf(line,sizeof(line),"%s (%lu)",focused->title,(unsigned long)focused->space_count);
 else
#endif
 if(frame.count)snprintf(line,sizeof(line),"Session %lu",(unsigned long)frame.selected+1);else snprintf(line,sizeof(line),"Pi watcher");
 text(line,53,strlen(line)>24?1.6f:2.5f,0xef7d);
 for(unsigned i=0;i<frame.count;i++)canvas.fillCircle(233+(static_cast<int>(i)-(static_cast<int>(frame.count)-1)/2)*12,91,i==frame.selected?4:2,(uint16_t)0xb43a);
 text(labels[state],340,state==4?2.5f:3.0f,state>=3 && state<=6 && state!=5?0xf672:0xef7d);
 const char* detail="Nothing needs you yet";
 if(state==3)detail=(frame.flags&2)?"Acknowledged; still waiting":"Waiting for your input";
 else if(state==4){snprintf(line,sizeof(line),"%lu pending request%s",(unsigned long)frame.pending,frame.pending==1?"":"s");detail=(frame.flags&2)?"Acknowledged; still waiting":line;}
 else if(state==5)detail="Session complete";
 else if(state==6)detail=(frame.flags&2)?"Acknowledged; failure retained":"Check this session";
 else if(state==7)detail="Waiting for fresh status";
 else if(state==8)detail=(frame.flags&16)?"Request retained":"Reconnect to your Mac";
 else if(state==9)detail="Start the USB host bridge";
 if((frame.flags&4) && state<7)detail="Source status incomplete";
#ifdef WATCHER_GHOST_PAGES
 if(focus_transition.previewing()||carousel.previewing())detail=paging_event?"Waiting for selection":"Preview; release to switch";
#endif
 text(detail,381,1.5f,0xbdd7);
 const char* feedback_label="Haptics off";
#if defined(WATCHER_HAPTICS) && !defined(WATCHER_FACTORY_ACCEPTANCE)
 feedback_label=audio.ready()?"Haptics on":"Feedback fault";
#endif
 if(frame.flags&1)feedback_label="Muted";
#ifdef WATCHER_GHOST_PAGES
 if(focused){
  unsigned local_waiting=0;
  for(unsigned i=0;i<frame.count;i++)if(shown_mask&(1u<<i)){const auto* row=ghost_page.get(i,frame);if(i!=frame.selected&&row&&row->pending)local_waiting++;}
  const unsigned hidden_waiting=frame.other>local_waiting?frame.other-local_waiting:0;
  snprintf(line,sizeof(line),"%lu offscreen / %u waiting / %s",(unsigned long)(frame.count-shown_count),hidden_waiting,feedback_label);
 }else
#endif
 if(frame.other)snprintf(line,sizeof(line),"%lu elsewhere / %s",(unsigned long)frame.other,feedback_label);
 else snprintf(line,sizeof(line),"%lu / %lu / %s",(unsigned long)(frame.count?frame.selected+1:0),(unsigned long)frame.count,feedback_label);
#ifdef WATCHER_MIC_CAPTURE
 const auto* voice_label=voice.label();
 // Terminal diagnostics survive link retirement and hardware faults; never imply capture readiness.
 if(const auto* failure=voice.failure_label()){text(failure,410,1.3f,0xf672);}
 else if(linked&&!(frame.flags&1)&&button_kind!=4&&!audio.fault()&&voice_label){text(voice_label,410,1.3f,0xf672);}
 else{text(line,410,1.4f,0xa514);}
#else
 text(line,410,1.4f,0xa514);
#endif
#if defined(WATCHER_TOUCH_DIAGNOSTICS) && !defined(WATCHER_TOUCH_TIMING_DIAGNOSTICS)
 bool show_touch=true;
#ifdef WATCHER_MIC_CAPTURE
 show_touch=!voice.consuming()&&!voice.busy();
#endif
 if(show_touch){char top[48],bottom[48];watcher_touch::diagnostic_lines(top,bottom,touch_sampler,swipe,touch_eligible,touch_blocked);text(top,427,1.0f,0xbdd7);text(bottom,443,1.0f,0xbdd7);}
#endif
 canvas.pushSprite(0,0);
#ifdef WATCHER_TOUCH_TIMING_DIAGNOSTICS
 touch_timing.paint(frame.selected,ms(),linked);
#endif
#ifdef WATCHER_GHOST_PAGES
 displayed_preview=focus_transition.previewing()||carousel.previewing();
 if(!linked||(paging_event&&frame.event_ack>=paging_event))paging_event=0;
#endif
 displayed_seq=linked?frame.seq:0;
#ifdef WATCHER_TOUCH_SWIPE
 displayed_selected=frame.selected;displayed_count=frame.count;displayed_rotation=rotation;
#endif
}
