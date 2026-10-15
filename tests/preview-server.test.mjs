import test from 'node:test';
import assert from 'node:assert/strict';
import { get } from 'node:http';
import { createPreviewServer } from '../preview/server-core.mjs';
for(const allowBlobImages of [false,true])test(`preview server stays allowlisted (export=${allowBlobImages})`,async t=>{
 const server=createPreviewServer({allowBlobImages});
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
 t.after(()=>new Promise(resolve=>server.close(resolve)));
 const base=`http://127.0.0.1:${server.address().port}`;
 const response=await fetch(base+'/');assert.equal(response.status,200);
 const csp=response.headers.get('content-security-policy');assert.match(csp,/connect-src 'none'/);assert.equal(csp.includes('blob:'),allowBlobImages);
 for(const path of ['/chrome-harness.mjs','/server-core.mjs','/export-ghost.mjs','/docs/recovery.md','/?file=anything'])assert.equal((await fetch(base+path)).status,404);
 assert.equal((await fetch(base+'/',{method:'POST'})).status,405);
 // Fetch normalizes Host; use the raw HTTP client to exercise the actual guard.
 const badHost=await new Promise((resolve,reject)=>{get(base+'/',{headers:{Host:'example.com'}},res=>{res.resume();res.on('end',()=>resolve(res.statusCode));}).on('error',reject);});
 assert.equal(badHost,403);
 assert.equal((await fetch(base+'/screen.mjs')).status,200);
});
