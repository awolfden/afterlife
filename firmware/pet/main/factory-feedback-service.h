#pragma once
#include "factory-feedback-engine.h"
#include "haptic-gate.h"
#include "freertos/task.h"
#include "esp_timer.h"
namespace watcher_feedback {
// Dormant at boot. Only the owner task accesses codec/IOE after board initialization.
// Cancellation is out-of-band and generation-tagged; no timer callback performs I2C.
class Service {
 mutable portMUX_TYPE lock_=portMUX_INITIALIZER_UNLOCKED;
 TaskHandle_t owner_=nullptr;
 Engine engine_;
 Engine::Result result_{};
 uint32_t generation_=0;
 int64_t deadline_=0;
 bool cancelled_=true,closing_=false,busy_=false,init_attempted_=false;
 unsigned pending_=0,outputs_=3;
 HapticGate haptic_gate_; // Only main-loop entry points use this gate.
 static int64_t now(){return esp_timer_get_time()/1000;}
 static void wait(unsigned ms){vTaskDelay(pdMS_TO_TICKS(ms)+1);}
 struct Active {Service* service;uint32_t generation;};
 static bool allowed(void* arg){
  auto& a=*static_cast<Active*>(arg);auto& s=*a.service;
  portENTER_CRITICAL(&s.lock_);
  const bool ok=!s.cancelled_&&!s.closing_&&s.generation_==a.generation&&now()<s.deadline_;
  portEXIT_CRITICAL(&s.lock_);return ok;
 }
 void publish(){portENTER_CRITICAL(&lock_);result_=engine_.status();busy_=false;portEXIT_CRITICAL(&lock_);}
 static void entry(void* ctx){static_cast<Service*>(ctx)->loop();}
 void process(){
   portENTER_CRITICAL(&lock_);
   const bool cancel=cancelled_||closing_||now()>=deadline_;
   const unsigned job=cancel?0:pending_;pending_=0;
   const uint32_t generation=generation_;busy_=job!=0;
   portEXIT_CRITICAL(&lock_);
   if(cancel||engine_.status().errors){
    if(!engine_.status().closed)engine_.shutdown();
   }else if(job){
    Active active{this,generation};engine_.run((job&1)!=0,(job&2)!=0,allowed,&active);
#ifdef WATCHER_FACTORY_ACCEPTANCE
    engine_.shutdown(); // One-shot acceptance also exercises the real cleanup path.
#endif
   }
   publish();
 }
 void loop(){
  for(;;){
   uint32_t notification=0;xTaskNotifyWait(0,UINT32_MAX,&notification,pdMS_TO_TICKS(25)+1);
   process();
  }
 }
 bool request(unsigned job){
  portENTER_CRITICAL(&lock_);
  job&=outputs_;
  const bool ok=job&&owner_&&!cancelled_&&!closing_&&!busy_&&!pending_&&!result_.errors&&now()<deadline_;
  if(ok)pending_=job;
  portEXIT_CRITICAL(&lock_);
  if(ok)xTaskNotify(owner_,1,eSetBits);
  return ok;
 }
 public:
 explicit Service(BoardIo io):engine_(io,now,wait){}
 void init(){
  if(init_attempted_)return;
  init_attempted_=true;
  if(xTaskCreate(entry,"feedback-owner",8192,this,5,&owner_)!=pdPASS){
   portENTER_CRITICAL(&lock_);result_.errors|=64;portEXIT_CRITICAL(&lock_);
  }
 }
 void context(int64_t valid_until,bool permitted){
  if(!permitted){stop();return;}
  portENTER_CRITICAL(&lock_);deadline_=valid_until;if(!closing_)cancelled_=false;portEXIT_CRITICAL(&lock_);
 }
 // Preference changes cancel admitted work before publishing the new mask.
 bool outputs(unsigned mask){
  if(mask>3)return false;
  portENTER_CRITICAL(&lock_);
  const bool changed=outputs_!=mask;
  if(changed){outputs_=mask;cancelled_=true;pending_=0;++generation_;}
  portEXIT_CRITICAL(&lock_);
  if(changed&&owner_)xTaskNotify(owner_,1,eSetBits);
  return true;
 }
 bool start(int64_t,unsigned mask=3){
#if defined(WATCHER_HAPTICS) || defined(WATCHER_FACTORY_HAPTIC_ACCEPTANCE)
  return request(mask&3);
#else
  return request(mask&1);
#endif
 }
 bool alert(const watcher_wire::Frame& frame,bool permitted=true,unsigned mask=2){
  const bool eligible=haptic_gate_.take(frame);
#ifndef WATCHER_FACTORY_ACCEPTANCE
#ifndef WATCHER_HAPTICS
  mask&=1; // A normal speaker-only build can never request the motor.
#endif
  return eligible&&permitted&&request(mask);
#else
  (void)eligible;(void)permitted;(void)mask;return false;
#endif
 }
 void tick(int64_t,bool permitted){if(!permitted)stop();}
 void stop(){
  portENTER_CRITICAL(&lock_);
  const bool changed=!cancelled_||pending_;cancelled_=true;pending_=0;if(changed)++generation_;
  portEXIT_CRITICAL(&lock_);
  if(changed&&owner_)xTaskNotify(owner_,1,eSetBits);
 }
 bool deinit(){
  portENTER_CRITICAL(&lock_);closing_=true;portEXIT_CRITICAL(&lock_);stop();
  if(owner_)xTaskNotify(owner_,1,eSetBits);
  const auto end=now()+2000;
  while(now()<end){portENTER_CRITICAL(&lock_);const bool done=!busy_&&result_.closed;
   portEXIT_CRITICAL(&lock_);if(done)return true;wait(1);}
  return false;
 }
 // Available to accept a request, not a claim of physical audio or hardware initialization.
 bool ready()const{portENTER_CRITICAL(&lock_);const bool ok=owner_&&!closing_&&!result_.errors;
  portEXIT_CRITICAL(&lock_);return ok;}
 bool fault()const{return status().errors!=0;}
 Engine::Result status()const{portENTER_CRITICAL(&lock_);const auto result=result_;portEXIT_CRITICAL(&lock_);return result;}
#ifdef WATCHER_FEEDBACK_HOST_TEST
 // Deterministic task interleavings in host mocks only; the engine rejects this
 // macro on ESP_PLATFORM, so this entry point cannot enter device firmware.
 void process_for_test(){process();}
#endif
};
}
