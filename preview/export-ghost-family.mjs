// Local-only original SVG exporter. Explicit output directory; no device or vendor writes.
import {createServer} from 'node:http';
import {readFile,mkdir,writeFile} from 'node:fs/promises';
import {resolve,join} from 'node:path';
import {createHash} from 'node:crypto';
import {withPreviewChrome} from './chrome-harness.mjs';
import {characters,states} from './ghost-family/expressions.mjs';
const args=process.argv.slice(2);
if(args.length!==2||args[0]!=='--output-dir')throw new Error('Usage: node preview/export-ghost-family.mjs --output-dir DIRECTORY');
const output=resolve(args[1]),names=characters.slice(1),sha=data=>createHash('sha256').update(data).digest('hex');
const assets=new Map(),sourceHashes={};
for(const name of ['expressions.mjs',...names.map(n=>n+'.svg')]){
 const bytes=await readFile(new URL('./ghost-family/'+name,import.meta.url));assets.set('/'+name,bytes);sourceHashes[name]=sha(bytes);
}
const server=createServer((request,response)=>{
 const path=new URL(request.url,'http://localhost').pathname;
 response.setHeader('Content-Security-Policy',"default-src 'none'; script-src 'self' 'unsafe-inline'; img-src blob: data:; connect-src 'self'; style-src 'unsafe-inline'");
 if(path==='/'){response.setHeader('Content-Type','text/html');response.end('<!doctype html><title>Local family export</title><script type="module">import {expressionSvg} from "/expressions.mjs";globalThis.expressionSvg=expressionSvg;</script>');return;}
 if(!assets.has(path)){response.writeHead(404);response.end();return;}
 response.setHeader('Content-Type',path.endsWith('.mjs')?'text/javascript':'image/svg+xml');response.end(assets.get(path));
});
await new Promise((ok,no)=>{server.once('error',no);server.listen(0,'127.0.0.1',ok);});
let frames;
try{frames=await withPreviewChrome(`http://127.0.0.1:${server.address().port}`,async({page})=>{
 const rendered=[];
 for(const name of names)for(const state of states)for(const blink of [false,true]){
 const result=await page('Runtime.evaluate',{awaitPromise:true,returnByValue:true,expression:`(async()=>{
    const name=${JSON.stringify(name)},state=${JSON.stringify(state)},blink=${blink};
    const source=await(await fetch('/'+name+'.svg')).text();
    const svg=expressionSvg(source,name,state,blink),url=URL.createObjectURL(new Blob([svg],{type:'image/svg+xml'}));
    try{const image=new Image();await new Promise((ok,no)=>{image.onload=ok;image.onerror=()=>no(new Error('SVG failed'));image.src=url;});
     const canvas=document.createElement('canvas');canvas.width=canvas.height=280;const ctx=canvas.getContext('2d',{willReadFrequently:true});ctx.drawImage(image,0,0);
     const bytes=ctx.getImageData(0,0,280,280).data;let text='';for(let i=0;i<bytes.length;i+=8192)text+=String.fromCharCode(...bytes.subarray(i,i+8192));
     return {name,state,blink,rgba:btoa(text)};
    }finally{URL.revokeObjectURL(url);}
 })()`});
 if(result.exceptionDetails)throw new Error(result.exceptionDetails.exception?.description??'Family export failed');rendered.push(result.result.value);
 }
 console.log('Rasterized all',rendered.length,'frames through bounded per-frame CDP messages.');return rendered;
},90000);}finally{await new Promise(ok=>server.close(ok));}
if(frames.length!==90)throw new Error('Incomplete family');
const baseline=JSON.parse(await readFile(new URL('../firmware/art/ghost.manifest.json',import.meta.url),'utf8'));
const background=baseline.frames[0].background,pool=[],rows=[],lookup=new Map(),references=[],evidence=[];
for(const frame of frames){
 const pixels=Buffer.from(frame.rgba,'base64');if(pixels.length!==280*280*4)throw new Error('Invalid raster');
 const decoded=Buffer.alloc(280*280*3),ids=[];let visible=0;
 for(let y=0;y<280;y++){
  const runs=[];let last=-2,count=0;
  for(let x=0;x<280;x++){
   const i=(y*280+x)*4,a=pixels[i+3],rgb=background.map((bg,c)=>Math.round((pixels[i+c]*a+bg*(255-a))/255));
   const color=a?((rgb[0]>>3)<<11)|((rgb[1]>>2)<<5)|(rgb[2]>>3):-1;
   if(color!==last&&count){runs.push([count|(last<0?32768:0),last<0?0:last]);count=0;}
   last=color;count++;
   if(a){visible++;const at=(y*280+x)*3;decoded[at]=1;decoded[at+1]=color&255;decoded[at+2]=color>>8;}
  }
  runs.push([count|(last<0?32768:0),last<0?0:last]);
  if(runs.reduce((n,r)=>n+(r[0]&32767),0)!==280)throw new Error('Row coverage');
  const key=JSON.stringify(runs);let id=lookup.get(key);
  if(id===undefined){id=rows.length;lookup.set(key,id);rows.push({offset:pool.length,count:runs.length});pool.push(...runs);}
  ids.push(id);
 }
 if(visible<5000||visible>60000)throw new Error('Unexpected body coverage');
 references.push(ids);evidence.push({character:frame.name,state:frame.state,blink:frame.blink,visiblePixels:visible,rgbaSha256:sha(pixels),decodedSha256:sha(decoded)});
}
if(rows.length>65535)throw new Error('Row ID capacity');
const binaryBytes=pool.length*4+rows.length*8+references.length*280*2;
if(binaryBytes>1600000)throw new Error('Family art exceeds conservative flash budget: '+binaryBytes);
let header='// Generated original artwork; shared immutable RGB565 rows. Do not hand-edit.\n#pragma once\n#include <cstdint>\n#include "ghost.generated.h"\nnamespace watcher_family {\nstruct Row { uint32_t offset; uint16_t count; };\ninline constexpr watcher_art::Run runs[] = {\n';
for(let i=0;i<pool.length;i+=12)header+=' '+pool.slice(i,i+12).map(r=>`{${r}}`).join(',')+',\n';
header+='};\ninline constexpr Row rows[] = {\n';for(let i=0;i<rows.length;i+=12)header+=' '+rows.slice(i,i+12).map(r=>`{${r.offset},${r.count}}`).join(',')+',\n';
header+='};\n// Character IDs 1..5; state IDs 0..8; blink 0..1; source y 0..279.\ninline constexpr uint16_t row_ids[5][9][2][280] = {\n';
let at=0;for(let c=0;c<5;c++){header+=' {\n';for(let s=0;s<9;s++){header+='  {\n';for(let b=0;b<2;b++)header+='   {'+references[at++].join(',')+'},\n';header+='  },\n';}header+=' },\n';}
header+='};\ninline constexpr unsigned row_count=sizeof(rows)/sizeof(rows[0]);\ninline constexpr unsigned run_count=sizeof(runs)/sizeof(runs[0]);\nstatic_assert(sizeof(runs)+sizeof(rows)+sizeof(row_ids)<=1600000,"Ghost family flash budget");\n}\n';
sourceHashes['export-ghost-family.mjs']=sha(await readFile(new URL('./export-ghost-family.mjs',import.meta.url)));
const manifest={schema:1,characters,states,width:280,height:280,origin:[140,130],background,format:'Deduplicated whole RGB565 rows with binary transparency; immutable flash row IDs',uniqueRows:rows.length,runs:pool.length,binaryBytes,sourceHashes,frames:evidence,headerSha256:sha(header),flagshipHeaderSha256:baseline.headerSha256,limitations:'Offline raster/export evidence, not device timing or optical FPS.'};
await mkdir(output,{recursive:true});await writeFile(join(output,'ghost-family.generated.h'),header);await writeFile(join(output,'ghost-family.manifest.json'),JSON.stringify(manifest,null,2)+'\n');
console.log(JSON.stringify({output,uniqueRows:rows.length,runs:pool.length,binaryBytes,headerSha256:manifest.headerSha256},null,2));
