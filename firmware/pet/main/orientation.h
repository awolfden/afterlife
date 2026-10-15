#pragma once
#include <cmath>
#include <stdint.h>
namespace watcher_orientation {
class Settled {
 unsigned current_=0,candidate_=0;int64_t since_=-1,last_=-1;
 public:
 unsigned current() const{return current_;}
 unsigned update(float x,float y,float z,int64_t now){
  if(now<0||now<last_)return current_;
  const bool gap=last_>=0&&now-last_>200;last_=now;
  const float ax=std::fabs(x),ay=std::fabs(y),az=std::fabs(z),magnitude=x*x+y*y+z*z;
  if(!std::isfinite(magnitude)||magnitude<0.64f||magnitude>1.44f||az>0.8f||std::fmax(ax,ay)<0.65f||std::fabs(ax-ay)<0.2f){since_=-1;return current_;}
  // StopWatch panel's vertical axis is opposite the IMU Y direction.
  // Owner-observed 0/180 inversion: swap only vertical mappings; X stays 1/3.
  const unsigned next=ax>ay?(x>0?1:3):(y>0?2:0);
  if(gap||since_<0||next!=candidate_){candidate_=next;since_=now;}
  if(now-since_>=400)current_=candidate_;
  return current_;
 }
 void unavailable(){since_=-1;last_=-1;}
};
}
