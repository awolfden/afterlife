import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {AttentionModel} from '../host/attention.mjs';
import {SourceClient} from '../host/source-client.mjs';
import {startHubServer} from '../host/hub-server.mjs';
import {registerWatcher} from '../host/pi-observer.mjs';

test('Unicode titles cross ASCII JSONL ingress; explicit unwatch removes the subscription',async t=>{
 const dir=mkdtempSync(join(tmpdir(),'watcher-identity-')),model=new AttentionModel();
 const hub=await startHubServer(model,dir,()=>0),id='a'.repeat(64),client=new SourceClient(id,hub.path);
 t.after(async()=>{client.close();await hub.close();rmSync(dir,{recursive:true,force:true});});
 const arrived=new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(new Error('identity deadline')),2000);const base=model.identity.bind(model);model.identity=(...args)=>{const result=base(...args);clearTimeout(timer);resolve(result);return result;};});
 client.identity(id,{spaceId:'b'.repeat(64),spaceTitle:'Wolf 🐺 café'});client.start();assert.equal(await arrived,true);
 assert.equal(model.snapshot().sessions[0].label,'Wolf 🐺 café');
 const removed=new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(new Error('unwatch deadline')),2000);const base=model.remove.bind(model);model.remove=(...args)=>{const result=base(...args);clearTimeout(timer);resolve(result);return result;};});
 client.close({unwatch:true});assert.equal(await removed,true);assert.equal(model.snapshot().sessions.length,0);
});
test('metadata lookup starts only after opt-in and late responses cannot update a disposed session',async()=>{
 const commands=new Map(),hooks=new Map(),identities=[];let lookups=0,finish;
 const client={start(){},register(){return true;},status(){},supervisorSync(){},close(){},identity(...value){identities.push(value);},connected:true};
 const pi={on(name,fn){hooks.set(name,fn);return()=>{};},events:{on(){return()=>{};},emit(){}},registerCommand(name,command){commands.set(name,command);}};
 const ctx={sessionManager:{getSessionId:()=> 'synthetic'},isIdle:()=>true,ui:{notify(){}}};
 const observer=registerWatcher(pi,{isChild:false,autoStart:false,spaceIdentity:true,clientFactory:()=>client,identityProvider:()=>{lookups++;return new Promise(resolve=>{finish=resolve;});}});
 assert.equal(lookups,0);await commands.get('watch').handler('on',ctx);assert.equal(lookups,1);
 await commands.get('watch').handler('off',ctx);finish({spaceId:'b'.repeat(64),spaceTitle:'Late'});await Promise.resolve();assert.equal(identities.length,0);observer.dispose();
});
