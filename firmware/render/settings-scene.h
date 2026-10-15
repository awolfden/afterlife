#pragma once
#include "scene-job.h"
#include "../pet/main/desk-settings.h"
#include "../pet/main/settings-input.h"
#include <cstdio>
namespace watcher_render {
inline bool settings_entry(SceneProgram& program,bool branded=false){
#ifdef WATCHER_AFTERLIFE
 if(branded)return program.text("Settings",414,2.0f,0xe71e,0xe71e);
#else
 (void)branded;
#endif
 return program.text("Settings",414,2.0f,0xb43a);
}
inline bool settings_scene(SceneProgram& program,const watcher_settings::Values& values,unsigned top,watcher_settings::Save save,bool forced,bool muted,bool incompatible,int offset=-1){
 if(offset==-1)offset=static_cast<int>(top*72);
 if(!values.valid()||top>2||offset<0||offset>144)return false;
 program=SceneProgram{};
 program.text("Settings",62,2.5f,0xef7d);
 const char* labels[]={"Vibration","Sounds","Animations","Cycle sessions","Brightness"};
 char line[64];
 const char* status=save==watcher_settings::Save::saved?"Saved on Mac":save==watcher_settings::Save::saving?"Saving...":save==watcher_settings::Save::unsaved?"Not saved on Mac":"Temporary until reconnect";
 program.text(status,348,1.3f,0xbdd7);
 if(forced||muted||incompatible){
  // Four fixed labels plus at most four partially visible row labels.
  std::snprintf(line,sizeof line,"%s%s%s",muted?"Muted / ":"",incompatible?"Host update required":forced?"Reduced motion forced by host":"Outputs silenced",incompatible&&forced?" / motion forced":"");
  program.text(line,376,1.0f,0xbdd7);
 }
 program.text("Back",414,2.0f,0xb43a);
 for(unsigned row=0;row<5;++row){
  const int y=136+static_cast<int>(row)*72-offset;
  if(y+24<=120||y>=336)continue;
  const char* value=row==4?(values.brightness==80?"Low":values.brightness==160?"Medium":"High"):((values.flags&(1u<<row))?"On":"Off");
  std::snprintf(line,sizeof line,"%s  %s",labels[row],value);
  program.text(line,y,2.0f,0xef7d,0x0842,120,336);
 }
 return program.valid();
}
} // namespace watcher_render
