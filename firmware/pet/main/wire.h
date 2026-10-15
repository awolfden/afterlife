#pragma once
#include <stdint.h>
#include <stddef.h>
namespace watcher_wire {
constexpr size_t max_line=192;
struct Frame {uint32_t boot,seq,selected,count,state,pending,other,flags,alert,event_ack;};
inline uint32_t crc(const char* s,size_t n){uint32_t c=0xffffffffu;for(size_t i=0;i<n;i++){c^=static_cast<uint8_t>(s[i]);for(unsigned b=0;b<8;b++)c=(c>>1)^((c&1)?0xedb88320u:0);}return c^0xffffffffu;}
inline bool number(const char*& p,const char* end,uint32_t& out){
 if(p==end || *p<'0' || *p>'9')return false;
 uint32_t value=0;
 do{const unsigned digit=*p-'0';if(value>(UINT32_MAX-digit)/10)return false;value=value*10+digit;++p;}while(p!=end && *p>='0' && *p<='9');out=value;return true;
}
inline bool parse(const char* line,size_t length,Frame& result){
 if(length<4 || length>=max_line || line[0]!='W' || line[1]!='1' || line[2]!=' ')return false;
 const char* p=line+3;const char* end=line+length;uint32_t v[11];const char* checksum_start=nullptr;
 for(unsigned i=0;i<11;i++){if(i==10)checksum_start=p;if(!number(p,end,v[i]))return false;if(i!=10){if(p==end || *p++!=' ')return false;}}
 if(p!=end || crc(line,static_cast<size_t>(checksum_start-line))!=v[10])return false;
 if(!v[0] || !v[1] || v[3]>16 || (v[3] && v[2]>=v[3]) || (!v[3] && v[2]) || v[4]>9 || v[5]>256 || v[6]>15 || v[7]>31)return false;
 if((!v[3] && v[4]!=9) || (v[3] && v[4]==9))return false;
 result={v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8],v[9]};return true;
}
// Nonzero alert IDs are host-issued, confirmed intents, independent of the viewed root.
// 'other' permits a known qualifying request elsewhere, not an alert inferred from its count.
inline bool alert_eligible(const Frame& f){
 return f.count && !(f.flags&1) && f.state<8 && (f.state==3 || f.state==4 || f.state==6 || f.other>0);
}
// No wrap acceptance. Exhaustion requires a new boot/transport session.
inline bool fresh(const Frame& f,uint32_t boot,uint32_t previous){return f.boot==boot && f.seq>previous;}
} // namespace watcher_wire
