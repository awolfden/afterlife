// Offline original-art export. Native connection text is deliberately NOT rasterized.
import {createServer} from 'node:http';
import {readFile,mkdir,writeFile} from 'node:fs/promises';
import {resolve,join} from 'node:path';
import {createHash} from 'node:crypto';
import {withPreviewChrome} from './chrome-harness.mjs';
const args=process.argv.slice(2);
if(args.length!==2||args[0]!=='--output-dir')throw Error('Usage: node preview/export-afterlife.mjs --output-dir DIRECTORY');
const sha=b=>createHash('sha256').update(b).digest('hex'),source=await readFile(new URL('./afterlife/disconnected.svg',import.meta.url),'utf8');
if((source.match(/<text\b/g)||[]).length!==1||!source.includes('>Connect to Pi</text>'))throw Error('Unexpected prompt inventory');
const art=source.replace(/<text\b[^>]*>Connect to Pi<\/text>/,'');
const server=createServer((req,res)=>{if(req.url==='/'){res.setHeader('Content-Type','text/html');res.end('<!doctype html><title>Offline Afterlife export</title>');}else{res.writeHead(404);res.end();}});
await new Promise(r=>server.listen(0,'127.0.0.1',r));let rgba;
try{rgba=await withPreviewChrome(`http://127.0.0.1:${server.address().port}`,async({page})=>{
 const value=await page('Runtime.evaluate',{awaitPromise:true,returnByValue:true,expression:`(async()=>{const url=URL.createObjectURL(new Blob([${JSON.stringify(art)}],{type:'image/svg+xml'}));try{const image=new Image();image.src=url;await image.decode();const c=document.createElement('canvas');c.width=c.height=466;const ctx=c.getContext('2d');ctx.fillStyle='#100d19';ctx.fillRect(0,0,466,466);ctx.drawImage(image,0,0);const b=ctx.getImageData(0,0,466,466).data;let s='';for(let i=0;i<b.length;i+=8192)s+=String.fromCharCode(...b.subarray(i,i+8192));return btoa(s);}finally{URL.revokeObjectURL(url);}})()`});
 if(value.exceptionDetails)throw Error('SVG raster failed');return Buffer.from(value.result.value,'base64');
});}finally{await new Promise(r=>server.close(r));}
if(rgba.length!==466*466*4)throw Error('Raster dimensions');
const pixels=Buffer.alloc(466*466*2),runs=[],offsets=[0];
for(let y=0;y<466;y++){
 let last=-1,n=0;
 for(let x=0;x<466;x++){const p=(y*466+x)*4;if(rgba[p+3]!==255)throw Error('Opaque art required');const color=((rgba[p]>>3)<<11)|((rgba[p+1]>>2)<<5)|(rgba[p+2]>>3);pixels.writeUInt16LE(color,(y*466+x)*2);if(n&&color!==last){runs.push([n,last]);n=0;}last=color;n++;}
 runs.push([n,last]);offsets.push(runs.length);
}
let header='// Generated original Afterlife artwork, excluding native connection text.\n#pragma once\n#include "ghost.generated.h"\nnamespace watcher_afterlife {\ninline constexpr unsigned width=466,height=466;\ninline constexpr watcher_art::Run runs[]={\n';
for(let i=0;i<runs.length;i+=12)header+=' '+runs.slice(i,i+12).map(r=>`{${r}}`).join(',')+',\n';
header+='};\ninline constexpr uint32_t offsets[467]={'+offsets.join(',')+'};\ninline constexpr unsigned run_count=sizeof(runs)/sizeof(runs[0]);\nstatic_assert(sizeof(runs)+sizeof(offsets)<=150000,"Afterlife art flash budget");\n}\n';
const binaryBytes=runs.length*4+offsets.length*4;if(binaryBytes>150000)throw Error('Art budget exceeded');
const manifest={schema:1,width:466,height:466,format:'Opaque RGB565 row RLE, immutable flash offsets; no decoded runtime canvas',runs:runs.length,binaryBytes,sourceSha256:sha(source),exporterSha256:sha(await readFile(new URL('./export-afterlife.mjs',import.meta.url))),headerSha256:sha(header),decodedSha256:sha(pixels),prompt:{text:'Connect to Pi',rendering:'Native SceneProgram text; excluded from artwork'},limitations:'Offline export, not hardware performance evidence'};
const out=resolve(args[1]);await mkdir(out,{recursive:true});await writeFile(join(out,'afterlife.generated.h'),header);await writeFile(join(out,'afterlife.manifest.json'),JSON.stringify(manifest,null,2)+'\n');console.log(JSON.stringify(manifest,null,2));
