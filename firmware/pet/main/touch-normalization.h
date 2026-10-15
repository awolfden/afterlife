#pragma once
#include "swipe.h"
namespace watcher_touch {
// Owner-only interior qualification envelope, NOT controller maxima. The small
// asymmetry reflects observed placements, not a certified difference in axes.
// Unknown extreme-edge values remain errors; never clamp or fit to extrema.
constexpr int measured_raw_x_max=460,measured_raw_y_max=461;
inline Decoded decode_measured_packet(const uint8_t* b,unsigned length){
 if(!b||length!=6)return {{},reject_packet};
 if(b[0]!=1)return decode_packet(b,length); // Unchanged count rejection / genuine release.
 const int x=((b[1]&15)<<8)|b[2],y=((b[3]&15)<<8)|b[4];
 const unsigned rejection=(x>measured_raw_x_max?static_cast<unsigned>(reject_x):0u)|(y>measured_raw_y_max?static_cast<unsigned>(reject_y):0u);
 if(rejection)return {{},rejection};
 // Fixed display-sized raw units -> existing recognition units, BEFORE orient().
 // Division is not calibrated from sampled extrema; thresholds remain 50/25/12.
 return {{Kind::contact,x/2,y/2},0};
}
}
