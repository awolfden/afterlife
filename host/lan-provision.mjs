import {randomInt} from 'node:crypto';
import {crc32} from './wire.mjs';

// Fixed-size, chunked RAM bootstrap. Caller must hold the delivery lock and send
// ONLY on the descriptor-verified physical USB transport, never on a LAN socket.
// Returning bytes is not evidence that the badge applied them. Never log packets.
export function encodeProvision({ssid,password,address,port,identity,key},{boot,transaction=randomInt(1,0x100000000)}){
 const fail=()=>{throw new Error('Invalid RAM provisioning configuration');};
 if(typeof ssid!=='string'||/[\p{Cc}\p{Cf}\p{Cs}\p{Zl}\p{Zp}]/u.test(ssid)||!ssid.length||Buffer.byteLength(ssid)>32||
    typeof password!=='string'||!/^[\x20-\x7e]{8,63}$/.test(password)||
    typeof identity!=='string'||!/^[0-9a-f]{64}$/.test(identity)||/^0+$/.test(identity)||
    !Buffer.isBuffer(key)||key.length!==32||key.every(n=>n===0)||
    !Number.isInteger(port)||port<1||port>65535||
    !Number.isInteger(boot)||boot<1||boot>0xffffffff||!Number.isInteger(transaction)||transaction<1||transaction>0xffffffff)fail();
 const parts=typeof address==='string'?address.split('.'):[];
 if(parts.length!==4||parts.some(n=>!/^\d+$/.test(n)||String(Number(n))!==n||Number(n)>255))fail();
 const ip=parts.map(Number),[a,b]=ip;
 if(!(a===10||(a===172&&b>=16&&b<=31)||(a===192&&b===168)))fail();
 const data=Buffer.alloc(168),ssidBytes=Buffer.from(ssid,'utf8');
 data[0]=1;data[1]=ssidBytes.length;data[2]=password.length;ssidBytes.copy(data,3);
 data.write(password,35,63,'ascii');Buffer.from(ip).copy(data,98);data.writeUInt16BE(port,102);
 key.copy(data,104);Buffer.from(identity,'hex').copy(data,136);
 try{
  return Array.from({length:6},(_,index)=>{
   const prefix=`P1 ${boot} ${transaction} ${index} ${data.subarray(index*32,Math.min(168,(index+1)*32)).toString('hex')} `;
   return `${prefix}${crc32(prefix)}\n`;
  });
 }finally{data.fill(0);ssidBytes.fill(0);}
 // JS strings and caller-owned inputs cannot be reliably erased; bounded lifetime
 // and no logging are required. Network-owner copies need their own cleanup.
}
