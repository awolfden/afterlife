import {randomInt} from 'node:crypto';
import {encodeProvision} from './lan-provision.mjs';
import {crc32} from './wire.mjs';
// Only call these methods for bytes from the exclusively owned physical USB pipe.
// LAN data must never acknowledge provisioning or request new credential packets.
export class LanBootstrap {
 #config;#pending=null;#paired=false;#error=null;#unacknowledgedAttempts=0;
 constructor(config){encodeProvision(config,{boot:1,transaction:1});this.#config=config;}
 usbStatus(status,now,send){
  if(status.capabilities&64){this.#error='Badge network owner fault; inspect before retrying';return;}
  if(!(status.capabilities&16)){this.#error='Badge firmware has no LAN support';return;}
  if(this.#paired&&(status.capabilities&32))return;
  if(this.#paired){this.#paired=false;this.#pending=null;}
  if(this.#unacknowledgedAttempts>=9){this.#error='Repeated RAM provisioning failure; restart bridge after investigating';return;}
  if(!this.#pending||this.#pending.boot!==status.boot)this.#pending={boot:status.boot,transaction:randomInt(1,0x100000000),attempts:0,next:now};
  const pending=this.#pending;if(now<pending.next)return;
  if(pending.attempts>=3){this.#error='RAM provisioning was not acknowledged';return;}
  // One bounded USB write, preserving chunk contiguity with normal W1 traffic.
  const bytes=encodeProvision(this.#config,pending).join('');
  send(bytes);this.#unacknowledgedAttempts++;pending.attempts++;pending.next=now+5000;this.#error=null;
 }
 usbAck(line){
  const match=/^P2 ([1-9][0-9]{0,9}) ([1-9][0-9]{0,9}) 1 ([0-9]{1,10})$/.exec(line);
  if(!match||!this.#pending)return false;
  const boot=Number(match[1]),transaction=Number(match[2]),checksum=Number(match[3]);
  if(boot>0xffffffff||transaction>0xffffffff||checksum>0xffffffff||crc32(line.slice(0,line.lastIndexOf(' ')+1))!==checksum||
     boot!==this.#pending.boot||transaction!==this.#pending.transaction)return false;
  this.#paired=true;this.#pending=null;this.#error=null;this.#unacknowledgedAttempts=0;return true;
 }
 status(){return {paired:this.#paired,pending:!!this.#pending,error:this.#error};}
 close(){this.#config=null;this.#pending=null;this.#paired=false;}
}
