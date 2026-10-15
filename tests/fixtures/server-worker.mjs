// Offline process fixture: no sockets, USB, provisioning or real sessions.
import {writeFileSync} from 'node:fs';
import {join} from 'node:path';
import {serveOwner} from '../../host/server-worker.mjs';
serveOwner(process,async options=>{
 let settle;const done=new Promise(resolve=>{settle=resolve;});
 return {done,status:()=>({lan:{paired:false}}),close:async()=>{writeFileSync(join(options.directory,'closed'),'closed');settle();}};
});
