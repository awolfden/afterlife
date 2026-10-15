// Display-only waiting pose. No timers, random state, queues or connection authority.
#pragma once
#include <cmath>
#include <cstdint>
namespace watcher_render {
struct ConnectingPose {int x=233,y=395;unsigned dots=3;bool blink=false;};
inline ConnectingPose connecting_pose(int64_t milliseconds,bool reduced){
 ConnectingPose pose;
 if(reduced||milliseconds<0)return pose;
 const double t=static_cast<double>(milliseconds)/1000.0;
 pose.x+=static_cast<int>(std::lround(24*std::sin(t*.38)+8*std::sin(t*.73)));
 pose.y+=static_cast<int>(std::lround(5*std::sin(t*.6)+3*std::cos(t*.31)));
 pose.dots=static_cast<unsigned>((milliseconds/550)%4);
 pose.blink=milliseconds%5700>5520;
 return pose;
}
}
