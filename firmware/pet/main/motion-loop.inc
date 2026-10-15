// New retained-frame service. No SceneJob, TransferJob, canvas or band copies.
struct MotionSurface {
 void clip(const watcher_render::PaintRect& r){display.setClipRect(r.x,r.y,r.right-r.x,r.bottom-r.y);}
 void clear(const watcher_render::PaintRect& r,uint16_t color){display.fillRect(r.x,r.y,r.right-r.x,r.bottom-r.y,color);}
 void span(int x,int y,int n,uint16_t color){display.fillRect(x,y,n,1,color);}
 void shadow(){display.fillEllipse(233,320,60,9,static_cast<uint16_t>(0x28e7));}
 void circle(int x,int y,int r,uint16_t color,bool filled){if(filled)display.fillCircle(x,y,r,color);else display.drawCircle(x,y,r,color);}
 void text(const char* value,int y,float size,uint16_t color){::text(value,y,size,color);}
#ifdef WATCHER_AFTERLIFE
 void text_background(const char* value,int y,float size,uint16_t color,uint16_t background){::text(value,y,size,color,background);}
#endif
};
static MotionSurface motion_surface;
struct MotionMetrics {
 uint32_t frames=0,reused=0,discarded=0;
 int64_t started=0,last_started=0,frame_max=0,interval_max=0,prepare_max=0,paint_max=0,submit_max=0,fence_max=0;
 uint64_t dirty_pixels=0,visited_runs=0;
 void increment(uint32_t& n){if(n!=UINT32_MAX)++n;}
 void maximum(int64_t& target,int64_t start){const auto elapsed=esp_timer_get_time()-start;if(elapsed>target)target=elapsed;}
};
static MotionMetrics motion_metrics;
static int64_t motion_due=0;
static bool motion_changed=false;
static void motion_fail(int64_t now){bounded_forget(now);bounded_fault=true;bounded_display.terminal();motion_frame.reset();}
static void motion_publish(){
 if(!bounded_authority.publish(bounded_identity(ms()),frame.seq,true)){bounded_observe(ms());return;}
 if(!motion_frame.commit()){motion_fail(ms());return;}
 const auto& shown=bounded_authority.completed();
#ifdef WATCHER_GHOST_FAMILY
 if(shown.identity.linked)last_published_character=bounded_program.family_selection;
#endif
 displayed_seq=bounded_authority.shown();displayed_selected=shown.identity.selected;
 displayed_count=shown.identity.count;displayed_rotation=shown.identity.rotation;displayed_preview=shown.preview;
 if(bounded_authority.clears_paging(paging_event)||(!shown.identity.linked&&paging_event==shown.paging_event))paging_event=0;
#ifdef WATCHER_TOUCH_TIMING_DIAGNOSTICS
 touch_timing.paint(shown.identity.selected,ms(),shown.identity.linked,shown.event_ack);
#endif
 if(motion_changed)motion_metrics.increment(motion_metrics.frames);else motion_metrics.increment(motion_metrics.reused);
 motion_metrics.maximum(motion_metrics.frame_max,motion_metrics.started);
}
static void bounded_render(int64_t now,unsigned interval,int64_t& rendered){
#ifdef WATCHER_TOUCH_TIMING_DIAGNOSTICS
 const auto began=ms();
 struct Measure {int64_t began;~Measure(){touch_timing.render(began,ms());}} measure{began};
#endif
 bounded_observe(now);
 if(exit_requested)bounded_forget(now);
 if(bounded_display.faulted()){motion_fail(now);return;}
 if(bounded_fault)return;
 if(bounded_display.pending()){
  const auto start=esp_timer_get_time();const bool idle=bounded_display.finish_band();
  motion_metrics.maximum(motion_metrics.fence_max,start);
  if(!idle){if(bounded_display.faulted())motion_fail(ms());return;}
 }
 if(bounded_authority.active()){
  if(bounded_authority.invalid()){
   if(!bounded_authority.retire(true)){motion_fail(ms());return;}
   motion_frame.reset();motion_metrics.increment(motion_metrics.discarded);motion_due=0;
  }else{motion_publish();return;}
 }
 if(exit_requested)return;
 if(bounded_rotation!=rotation){
  if(!bounded_display.apply_rotation(bounded_rotation)){motion_fail(ms());return;}
  rotation=bounded_rotation;motion_frame.reset();motion_due=0;
 }
 if(bounded_brightness){
  if(!bounded_display.apply_brightness(brightness)){motion_fail(ms());return;}
  bounded_brightness=false;return; // Let normal services run before the fence.
 }
 const auto start=esp_timer_get_time();
 if(start<0||start<motion_metrics.last_started){motion_fail(ms());return;}
 // A shorter interactive period takes effect immediately; no queue/catch-up.
 const int64_t period=static_cast<int64_t>(interval)*1000;
 if(motion_metrics.last_started&&motion_due>motion_metrics.last_started+period)motion_due=motion_metrics.last_started+period;
 if(start<motion_due)return;
 watcher_render::RenderSnapshot snapshot;
 if(!prepare_bounded_scene(ms(),bounded_program,snapshot)){
  if(!bounded_program.valid())motion_fail(ms());
  return;
 }
 motion_metrics.maximum(motion_metrics.prepare_max,start);
 if(!motion_frame.prepare(bounded_program)||!bounded_authority.start(snapshot)){motion_fail(ms());return;}
 motion_metrics.started=start;
 if(motion_metrics.last_started&&start-motion_metrics.last_started>motion_metrics.interval_max)motion_metrics.interval_max=start-motion_metrics.last_started;
 motion_metrics.last_started=start;rendered=now;
 // Advance an absolute deadline, but never replay expired animation frames.
 motion_due=motion_due?motion_due+period:start+period;
 if(motion_due<=start)motion_due=start+period;
 motion_changed=!motion_frame.dirty().empty();
 if(motion_changed&&!bounded_display.begin_frame(motion_frame.dirty())){motion_fail(ms());return;}
 const auto paint_start=esp_timer_get_time();
 if(!motion_frame.paint(motion_sprites,background,motion_surface)){motion_fail(ms());return;}
 motion_metrics.maximum(motion_metrics.paint_max,paint_start);
 motion_metrics.visited_runs+=motion_frame.visited_runs();motion_metrics.dirty_pixels+=motion_frame.dirty().area();
 // Last authoritative check before any changed pixels are submitted. Composition
 // was RAM-only with auto-display disabled and exclusive framebuffer ownership.
 if(!bounded_authority.begin_pixels(bounded_identity(ms()),frame.seq)){
  // Liveness can expire during RAM composition. Cancel without submitting;
  // this known dirty union requires a full repaint, not a terminal fault.
  if(motion_changed&&!bounded_display.abandon_frame()){motion_fail(ms());return;}
  if(!bounded_authority.retire(true)){motion_fail(ms());return;}
  motion_frame.reset();motion_due=0;motion_metrics.increment(motion_metrics.discarded);return;
 }
 displayed_seq=bounded_authority.shown();
 if(motion_changed){
  const auto submitted=esp_timer_get_time();
  if(!bounded_display.submit_frame()){motion_fail(ms());return;}
  motion_metrics.maximum(motion_metrics.submit_max,submitted);
  const auto fence=esp_timer_get_time();const bool idle=bounded_display.finish_band();
  motion_metrics.maximum(motion_metrics.fence_max,fence);
  if(!idle){if(bounded_display.faulted())motion_fail(ms());return;}
 }
 motion_publish();
}
#ifdef WATCHER_MOTION_METRICS
// Count successfully published changed frames separately from no-op publications,
// NOT all transfers: initialization and invalidated transfers are excluded.
// Work counters include abandoned RAM work; frame maximum covers publications.
// Cumulative serialized fields saturate at u32; saturated deltas are unusable.
// USB-only nonblocking counters, no coordinates. Not optical scanout timing.
static void motion_report(int64_t now){
 static int64_t last=0;static uint32_t sequence=0;
 if(now<last||now-last<5000||sequence==UINT32_MAX)return;
 last=now;++sequence;
 const auto bounded=[](uint64_t n){return static_cast<unsigned long>(n>UINT32_MAX?UINT32_MAX:n);};
 char line[192];
 const int n=snprintf(line,sizeof(line),"R3 %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %u ",
  (unsigned long)boot_id,(unsigned long)sequence,bounded(now),(unsigned long)motion_metrics.frames,(unsigned long)motion_metrics.reused,(unsigned long)motion_metrics.discarded,
  bounded(motion_metrics.frame_max),bounded(motion_metrics.interval_max),bounded(motion_metrics.prepare_max),bounded(motion_metrics.paint_max),bounded(motion_metrics.submit_max),bounded(motion_metrics.fence_max),
  bounded(motion_metrics.dirty_pixels),bounded(motion_metrics.visited_runs),static_cast<unsigned>(bounded_display.fault()));
 if(n<=0||static_cast<size_t>(n)>=sizeof(line)-16)return;
 const int tail=snprintf(line+n,sizeof(line)-n,"%lu\n",(unsigned long)watcher_wire::crc(line,n));
 if(tail>0&&static_cast<size_t>(n+tail)<sizeof(line))usb_serial_jtag_write_bytes(line,n+tail,0);
}
#endif
