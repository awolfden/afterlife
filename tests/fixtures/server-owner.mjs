// Offline owner used only to prove child cleanup after this process is killed.
import {fork} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {OwnedServer} from '../../host/server-process.mjs';
const server=new OwnedServer({launch:()=>fork(fileURLToPath(new URL('./server-worker.mjs',import.meta.url)),[],{execArgv:[],stdio:['ignore','ignore','ignore','ipc']})});
await server.start({directory:process.argv[2],python:'/fixture/python',port:'/dev/cu.usbmodemTEST',lan:{ssid:'synthetic',password:'synthetic-pass',address:'192.168.1.2',port:0}});
process.send({type:'ready'});
process.once('disconnect',()=>void server.close());
