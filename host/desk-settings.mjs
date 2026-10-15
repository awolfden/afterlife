// Global preferences under the existing delivery owner; no independent lock/daemon.
import * as fs from 'node:fs';
import {join} from 'node:path';
import {randomUUID} from 'node:crypto';
import {DeliveryLedger} from './delivery-ledger.mjs';
export const SETTINGS_DEFAULTS=Object.freeze({vibration:true,sounds:false,animations:true,cycleSessions:false,brightness:160});
export const SETTINGS_MAX_REVISION=0xffffffff;
const KEYS=Object.keys(SETTINGS_DEFAULTS).sort().join(','),MAX_BYTES=1024;
export function validSettings(value){
 return !!value&&typeof value==='object'&&!Array.isArray(value)&&Object.keys(value).sort().join(',')===KEYS&&
  ['vibration','sounds','animations','cycleSessions'].every(key=>typeof value[key]==='boolean')&&[80,160,240].includes(value.brightness);
}
export function settingsValues(record){return Object.fromEntries(Object.keys(SETTINGS_DEFAULTS).map(key=>[key,record[key]]));}
export function validSettingsRecord(value){
 return !!value&&typeof value==='object'&&!Array.isArray(value)&&Object.keys(value).sort().join(',')==='animations,brightness,cycleSessions,revision,sounds,version,vibration'&&
  value.version===1&&Number.isInteger(value.revision)&&value.revision>0&&value.revision<=SETTINGS_MAX_REVISION&&validSettings(settingsValues(value));
}
export class DeskSettings {
 #owner;#directory;#path;#io;#data;#failed=false;
 constructor(directory,owner,{io=fs}={}){
  if(!(owner instanceof DeliveryLedger)||!owner.owns(directory))throw new Error('Settings require the active delivery owner');
  this.#owner=owner;this.#directory=directory;this.#path=join(directory,'settings.json');this.#io=io;
  const stat=io.lstatSync(directory);
  if(!stat.isDirectory()||stat.isSymbolicLink()||stat.uid!==process.getuid()||(stat.mode&0o777)!==0o700)throw new Error('Unsafe settings directory');
  let fd;
  try{fd=io.openSync(this.#path,fs.constants.O_RDONLY|fs.constants.O_NOFOLLOW|fs.constants.O_NONBLOCK);}
  catch(error){if(error.code!=='ENOENT')throw new Error('Unsafe or inaccessible settings file');}
  if(fd===undefined){this.#commit({version:1,revision:1,...SETTINGS_DEFAULTS});return;}
  try{
   const stat=io.fstatSync(fd);
   if(!stat.isFile()||stat.nlink!==1||stat.uid!==process.getuid()||(stat.mode&0o777)!==0o600||stat.size>MAX_BYTES)throw new Error('Unsafe settings file');
   const bytes=Buffer.alloc(MAX_BYTES+1);let count=0;
   while(count<bytes.length){const n=io.readSync(fd,bytes,count,bytes.length-count,null);if(!n)break;count+=n;}
   if(count>MAX_BYTES)throw new Error('Settings file exceeds capacity');
   let value;try{value=JSON.parse(bytes.subarray(0,count).toString('utf8'));}catch{throw new Error('Invalid settings file');}
   if(!validSettingsRecord(value))throw new Error('Invalid settings file');
   this.#data=Object.freeze(value);
  }finally{io.closeSync(fd);}
 }
 get failed(){return this.#failed||!this.#owner.owns(this.#directory);}
 snapshot(){return this.#data?Object.freeze({...this.#data}):null;}
 update(values){
  if(this.failed)throw new Error('Settings persistence unavailable');
  if(!validSettings(values))throw new TypeError('Invalid settings values');
  if(Object.keys(SETTINGS_DEFAULTS).every(key=>values[key]===this.#data[key]))return this.snapshot();
  if(this.#data.revision===SETTINGS_MAX_REVISION)throw new Error('Settings revision exhausted');
  this.#commit({version:1,revision:this.#data.revision+1,...values});return this.snapshot();
 }
 #commit(next){
  if(this.failed)throw new Error('Settings persistence unavailable');
  const io=this.#io,temp=join(this.#directory,`.settings-${randomUUID()}.tmp`);let fd;
  try{
   const bytes=JSON.stringify(next)+'\n';if(Buffer.byteLength(bytes)>MAX_BYTES)throw new Error('Settings capacity exceeded');
   fd=io.openSync(temp,fs.constants.O_WRONLY|fs.constants.O_CREAT|fs.constants.O_EXCL|fs.constants.O_NOFOLLOW,0o600);
   io.writeFileSync(fd,bytes);io.fsyncSync(fd);io.closeSync(fd);fd=undefined;
   io.renameSync(temp,this.#path);
   const parent=io.openSync(this.#directory,fs.constants.O_RDONLY|fs.constants.O_DIRECTORY|fs.constants.O_NOFOLLOW);
   try{io.fsyncSync(parent);}finally{io.closeSync(parent);}
   this.#data=Object.freeze({...next});
  }catch{this.#failed=true;throw new Error('Settings save failed; persistence disabled until restart');}
  finally{
   if(fd!==undefined){try{io.closeSync(fd);}catch{this.#failed=true;}}
   try{io.unlinkSync(temp);}catch(error){if(error.code!=='ENOENT')this.#failed=true;}
  }
 }
}
