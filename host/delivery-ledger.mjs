// Single-writer private, bounded metadata ledger. A claim is NOT proof of vibration.
import { createHash, randomUUID } from 'node:crypto';
import * as fs from 'node:fs';
import { join,resolve } from 'node:path';
const LIMIT=4096, MAX_BYTES=1024*1024;
const opaque=value=>typeof value==='string' && /^[a-f0-9]{64}$/.test(value);
export function deliveryKey(sessionId,itemId) {
  if(!opaque(sessionId)||!opaque(itemId))throw new TypeError('Invalid attention identity');
  return createHash('sha256').update(JSON.stringify(['watcher-delivery-v1',sessionId,itemId])).digest('hex');
}
export class DeliveryLedger {
  #directory; #path; #lock; #lockFd; #closed=false; #failed=false;
  #data={version:1,muted:false,notifications:[],acks:[]};
  constructor(directory) {
    const stat=fs.lstatSync(directory);
    if(!stat.isDirectory() || stat.uid!==process.getuid() || (stat.mode&0o777)!==0o700)throw new Error('Ledger needs an owned, non-symlink 0700 directory');
    this.#directory=directory;this.#path=join(directory,'delivery.json');this.#lock=join(directory,'delivery.lock');
    this.#lockFd=fs.openSync(this.#lock,fs.constants.O_CREAT|fs.constants.O_EXCL|fs.constants.O_WRONLY|fs.constants.O_NOFOLLOW,0o600);
    try {
      fs.writeFileSync(this.#lockFd,JSON.stringify({pid:process.pid,nonce:randomUUID()}));fs.fsyncSync(this.#lockFd);
      if(fs.existsSync(this.#path)) {
        const fd=fs.openSync(this.#path,fs.constants.O_RDONLY|fs.constants.O_NOFOLLOW|fs.constants.O_NONBLOCK);
        try {
          const s=fs.fstatSync(fd);
          if(!s.isFile() || s.nlink!==1 || s.uid!==process.getuid() || (s.mode&0o777)!==0o600 || s.size>MAX_BYTES)throw new Error('Unsafe ledger file');
          const bytes=Buffer.alloc(MAX_BYTES+1);let count=0;
          while(count<bytes.length){const n=fs.readSync(fd,bytes,count,bytes.length-count,null);if(!n)break;count+=n;}
          if(count>MAX_BYTES)throw new Error('Ledger byte capacity exceeded');
          const data=JSON.parse(bytes.subarray(0,count).toString('utf8'));
          if(!data || Object.keys(data).sort().join(',')!=='acks,muted,notifications,version' || data.version!==1 || typeof data.muted!=='boolean' || !Array.isArray(data.notifications) || !Array.isArray(data.acks) || data.notifications.length>LIMIT || data.acks.length>LIMIT)throw new Error('Invalid ledger');
          if(data.acks.some(key=>!opaque(key)) || new Set(data.acks).size!==data.acks.length)throw new Error('Invalid ACK ledger');
          const keys=new Set();
          for(const row of data.notifications) {
            if(!row || Object.keys(row).sort().join(',')!=='key,status' || !opaque(row.key) || !['claimed','accepted','suppressed'].includes(row.status) || keys.has(row.key))throw new Error('Invalid delivery record');
            keys.add(row.key);
          }
          this.#data=data;
        } finally {fs.closeSync(fd);}
      }
    } catch(error){this.close();throw error;}
  }
  #commit(next) {
    if(this.#closed || this.#failed)throw new Error('Ledger unavailable');
    const temp=join(this.#directory,`.delivery-${randomUUID()}.tmp`);
    let fd;
    try {
      const bytes=JSON.stringify(next)+'\n';if(Buffer.byteLength(bytes)>MAX_BYTES)throw new Error('Ledger byte capacity exceeded');
      fd=fs.openSync(temp,fs.constants.O_CREAT|fs.constants.O_EXCL|fs.constants.O_WRONLY|fs.constants.O_NOFOLLOW,0o600);
      fs.writeFileSync(fd,bytes);fs.fsyncSync(fd);fs.closeSync(fd);fd=undefined;
      fs.renameSync(temp,this.#path);
      const parent=fs.openSync(this.#directory,fs.constants.O_RDONLY);try{fs.fsyncSync(parent);}finally{fs.closeSync(parent);}
      this.#data=next;
    } catch(error){this.#failed=true;throw error;}
    finally {if(fd!==undefined)fs.closeSync(fd);try{fs.unlinkSync(temp);}catch(error){if(error.code!=='ENOENT')this.#failed=true;}}
  }
  // Read-only ownership witness for sibling settings storage; never acquires a second lock.
  owns(directory){return !this.failed&&typeof directory==='string'&&resolve(directory)===resolve(this.#directory);}
  get muted(){return this.#data.muted;}
  get failed(){return this.#failed || this.#closed;}
  acknowledged(sessionId,itemId){return this.#data.acks.includes(deliveryKey(sessionId,itemId));}
  // One atomic claim for a coalesced batch, BEFORE anything is transmitted.
  claim(intents,status='claimed') {
    if(this.failed)throw new Error('Ledger unavailable');
    if(!Array.isArray(intents)||intents.length>1024||!['claimed','suppressed'].includes(status))throw new TypeError('Invalid intent batch');
    const existing=new Set(this.#data.notifications.map(row=>row.key)),acked=new Set(this.#data.acks),added=[];
    for(const item of intents){const key=deliveryKey(item.sessionId,item.itemId);if(!existing.has(key)&&!acked.has(key)){existing.add(key);added.push(key);}}
    if(this.#data.notifications.length+added.length>LIMIT)throw new Error('Notification ledger full; explicit reconciliation required');
    if(added.length)this.#commit({...this.#data,notifications:[...this.#data.notifications,...added.map(key=>({key,status}))]});
    return Object.freeze(added);
  }
  accepted(keys) {
    if(!Array.isArray(keys)||keys.some(key=>!opaque(key)))throw new TypeError('Invalid receipt keys');
    const set=new Set(keys);let changed=false;
    const notifications=this.#data.notifications.map(row=>{if(set.has(row.key)&&row.status==='claimed'){changed=true;return {...row,status:'accepted'};}return row;});
    if(changed)this.#commit({...this.#data,notifications});
  }
  acknowledge(sessionId,itemId) {
    const key=deliveryKey(sessionId,itemId);if(this.#data.acks.includes(key))return;
    if(this.#data.acks.length>=LIMIT)throw new Error('ACK ledger full; explicit reconciliation required');
    this.#commit({...this.#data,acks:[...this.#data.acks,key]});
  }
  mute(value){if(typeof value!=='boolean')throw new TypeError('Invalid mute');if(value!==this.#data.muted)this.#commit({...this.#data,muted:value});}
  snapshot(){return structuredClone(this.#data);}
  close(){if(this.#closed)return;this.#closed=true;if(this.#lockFd!==undefined){fs.closeSync(this.#lockFd);this.#lockFd=undefined;fs.unlinkSync(this.#lock);}}
}
