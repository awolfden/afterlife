// Physical-row-band geometry for the pinned 468x468 framebuffer and466x466 canvas.
// Geometry only: caller must enforce reserved buffers, clean dirty state,
// exclusive ownership, clip restoration and underlying bus completion.
#pragma once
namespace watcher_render {
struct TransferBand { int x=0,y=0,width=0,height=0,physical_y=0,physical_rows=0; };
class TransferBands {
 static constexpr unsigned panel_size=468,canvas_size=466;
 unsigned rotation_=0,rows_=0,next_=0;
 bool valid_=false;
 public:
 bool begin(unsigned rotation,unsigned rows){
  rotation_=rotation;rows_=rows;next_=0;
  valid_=rotation<4&&rows>=2&&rows<=64&&!(rows&1u);return valid_;
 }
 bool valid()const{return valid_;}
 bool complete()const{return valid_&&next_>=panel_size;}
 bool next(TransferBand& result){
  result={};if(!valid_||next_>=panel_size)return false;
  while(next_<panel_size){
   const unsigned first=next_,end=(panel_size-first<rows_)?panel_size:first+rows_;
   next_=end;
   // Pinned framebuffer rotation: r1 flipsY then swaps; r2 flips both;
   // r3 flipsX then swaps. PhysicalY is logicalY for even rotations,
   // logicalX for odd rotations, mirrored for r2/r3.
   const unsigned low=(rotation_&2u)?panel_size-end:first;
   const unsigned high=(rotation_&2u)?panel_size-first:end;
   if(low>=canvas_size)continue; // At most the two untouched margin rows.
   const unsigned clippedEnd=high<canvas_size?high:canvas_size;
   result={0,0,static_cast<int>(canvas_size),static_cast<int>(canvas_size),static_cast<int>(first),static_cast<int>(end-first)};
   if(rotation_&1u){result.x=static_cast<int>(low);result.width=static_cast<int>(clippedEnd-low);}
   else{result.y=static_cast<int>(low);result.height=static_cast<int>(clippedEnd-low);}
   return true;
  }
  return false;
 }
};
} // namespace watcher_render
