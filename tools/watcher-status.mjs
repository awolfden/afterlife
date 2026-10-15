#!/usr/bin/env node
import {homedir} from 'node:os';
import {join,resolve} from 'node:path';
import {inspectHost,describeHostStatus} from '../host/host-status.mjs';
const args=process.argv.slice(2);let directory=join(homedir(),'.local/share/pi-watcher-pet/run'),json=false;
try{
 for(let i=0;i<args.length;i++){
  if(args[i]==='--json')json=true;
  else if(args[i]==='--state-dir'&&args[i+1]&&!args[i+1].startsWith('--'))directory=resolve(args[++i]);
  else throw Error('Usage: node tools/watcher-status.mjs [--state-dir DIRECTORY] [--json]');
 }
 const status=await inspectHost(join(directory,'watcher.sock'));
 if(!status){console.log(json?JSON.stringify({available:false}):'Host status unavailable. Start the host, or check that its state directory matches.');process.exitCode=1;}
 else{console.log(json?JSON.stringify(status):`${describeHostStatus(status)}; ${status.sessions} watched session(s).`);}
}catch(error){console.error(error.message);process.exitCode=1;}
