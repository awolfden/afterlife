// Included inside main's namespace, after preparation. No nested input service.
struct BoundedSurface {
 void clear_rows(unsigned y,unsigned rows,uint16_t color){canvas.fillRect(0,y,466,rows,color);}
 void span(int x,int y,int n,uint16_t color){canvas.fillRect(x,y,n,1,color);}
 void shadow(){canvas.fillEllipse(233,320,60,9,static_cast<uint16_t>(0x28e7));}
 void circle(int x,int y,int r,uint16_t color,bool filled){if(filled)canvas.fillCircle(x,y,r,color);else canvas.drawCircle(x,y,r,color);}
 void text(const char* value,int y,float size,uint16_t color){::text(value,y,size,color);}
};
static BoundedSurface bounded_surface;
static void bounded_fail(int64_t now){
 bounded_forget(now);bounded_fault=true;bounded_scene.cancel();bounded_transfer.abort();
 // Terminal until board restart. Keep all DMA/PSRAM resources pinned; no retry
 // or repair of an unknown dirty union and no further authority publication.
}
static void bounded_render(int64_t now,unsigned interval,int64_t& rendered){
#ifdef WATCHER_TOUCH_TIMING_DIAGNOSTICS
 const auto began=ms();
 struct Measure {int64_t began;~Measure(){touch_timing.render(began,ms());}} measure{began};
#endif
 const auto start=esp_timer_get_time();
 bounded_observe(now);
 if(bounded_display.faulted()){bounded_fail(now);return;}
 if(exit_requested)bounded_forget(now);
 if(bounded_fault)return;
 if(bounded_authority.invalid()&&bounded_authority.active()){
  bounded_scene.cancel();
  if(bounded_transferring){bounded_transfer.abort();if(bounded_transfer.pending())bounded_transfer.step(bounded_display);}
  if(bounded_display.pending()&&!bounded_transfer.pending())bounded_display.finish_band();
  if(bounded_display.faulted()){bounded_fail(ms());return;}
  if(!bounded_transfer.pending()&&!bounded_display.pending()){
   bounded_authority.retire(true);bounded_transferring=false;
  }
  return;
 }
 if(exit_requested)return;
 // A brightness command may have its own last transaction pending, independent
 // of TransferJob. Finish it before submitting pixels or changing geometry.
 if(bounded_display.pending()&&!bounded_transfer.pending()){
  if(!bounded_display.finish_band()){if(bounded_display.faulted())bounded_fail(ms());return;}
 }
 if(bounded_rotation!=rotation){
  if(bounded_authority.active()||bounded_transfer.pending())return;
  if(!bounded_display.apply_rotation(bounded_rotation)){if(bounded_display.faulted())bounded_fail(ms());return;}
  rotation=bounded_rotation;
 }
 if(bounded_brightness&&!bounded_transfer.pending()){
  if(!bounded_display.apply_brightness(brightness)){if(bounded_display.faulted())bounded_fail(ms());return;}
  bounded_brightness=false;
  if(!bounded_display.finish_band()){if(bounded_display.faulted())bounded_fail(ms());return;}
 }
 if(!bounded_authority.active()){
  if(now-rendered<interval)return;
  rendered=now;
  watcher_render::RenderSnapshot snapshot;
  if(!prepare_bounded_scene(now,bounded_program,snapshot)){
   if(!bounded_program.valid())bounded_fail(ms());
   return;
  }
  if(!bounded_authority.start(snapshot)||!bounded_scene.begin(bounded_program,background)){bounded_fail(ms());return;}
  bounded_transferring=false;
 }
 //6ms target + one bounded work unit's cost;8192-unit fail-safe even if clock
 // stalls. Actual worst unit time still requires target/hardware qualification.
 for(unsigned work=0;work<8192&&esp_timer_get_time()-start<6000;++work){
  bounded_observe(ms());
  if(bounded_authority.invalid())break;
  if(!bounded_scene.complete()){
   bounded_scene.step(1,bounded_surface);
   if(bounded_scene.phase()==watcher_render::ScenePhase::failed){bounded_fail(ms());break;}
   continue;
  }
  if(!bounded_transferring){
   if(!bounded_transfer.begin(rotation,16)){bounded_fail(ms());break;}
   bounded_transferring=true;
   if(!bounded_authority.begin_pixels(bounded_identity(ms()),frame.seq)){bounded_observe(ms());break;}
   displayed_seq=bounded_authority.shown(); // Quarantine BEFORE first visible band.
  }
  bounded_transfer.step(bounded_display);
  if(bounded_display.faulted()||bounded_transfer.phase()==watcher_render::TransferPhase::failed){bounded_fail(ms());break;}
  if(bounded_transfer.pending())break; // Return to normal services/yield; no unbounded DMA wait.
  if(bounded_transfer.publishable()){
   if(!bounded_authority.publish(bounded_identity(ms()),frame.seq,true)){bounded_observe(ms());break;}
   const auto& shown=bounded_authority.completed();
   displayed_seq=bounded_authority.shown();displayed_selected=shown.identity.selected;
   displayed_count=shown.identity.count;displayed_rotation=shown.identity.rotation;displayed_preview=shown.preview;
   if(bounded_authority.clears_paging(paging_event)||(!shown.identity.linked&&paging_event==shown.paging_event))paging_event=0;
#ifdef WATCHER_TOUCH_TIMING_DIAGNOSTICS
   touch_timing.paint(shown.identity.selected,ms(),shown.identity.linked,shown.event_ack);
#endif
   bounded_transferring=false;break;
  }
 }
}
