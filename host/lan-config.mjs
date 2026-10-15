import {openSync,fstatSync,readSync,closeSync,constants} from 'node:fs';
// Explicit caller-supplied file only. Never inspect Keychain, saved Wi-Fi networks,
// environment credentials, or unrelated configuration. Error text is secret-free.
export function readLanConfig(path){
 let fd;
 try{
  fd=openSync(path,constants.O_RDONLY|constants.O_NOFOLLOW|constants.O_NONBLOCK);
  const st=fstatSync(fd);
  if(!st.isFile()||st.uid!==process.getuid()||(st.mode&0o077)||st.size>1024)throw new Error();
  const raw=Buffer.alloc(1025);let value,total=0;
  try{
   while(total<raw.length){const n=readSync(fd,raw,total,raw.length-total,null);if(!n)break;total+=n;}
   if(total>1024)throw new Error();value=JSON.parse(raw.subarray(0,total).toString('utf8'));
  }finally{raw.fill(0);}
  if(!value||Array.isArray(value)||typeof value!=='object'||Object.keys(value).some(k=>!['ssid','password','address','port'].includes(k))||
     typeof value.ssid!=='string'||typeof value.password!=='string'||typeof value.address!=='string'||
     (value.port!==undefined&&(!Number.isInteger(value.port)||value.port<0||value.port>65535)))throw new Error();
  return value;
 }catch{throw new Error('LAN config must be a valid private owner-only JSON file with ssid, password, address and optional port');}
 finally{if(fd!==undefined)closeSync(fd);}
}
