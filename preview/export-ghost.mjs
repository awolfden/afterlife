// Export only our approved original character into firmware-friendly RGB565 spans.
// Uses isolated Chrome and a local preview; no device, profile, fonts or external assets.
import { withPreviewChrome } from './chrome-harness.mjs';
import { createPreviewServer } from './server-core.mjs';
import { mkdir, writeFile, readFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';
const destination=new URL('../firmware/art/',import.meta.url);
const states=['idle','thinking','working','needs-input','child-help','completed','failed','unavailable','offline'];
const sha=data=>createHash('sha256').update(data).digest('hex');
// A separate ephemeral export origin permits only locally created blob images.
// The user's existing preview and its stricter CSP are not changed.
const server=createPreviewServer({allowBlobImages:true});
await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(0,'127.0.0.1',resolve);});
let result;
try { result=await withPreviewChrome(`http://127.0.0.1:${server.address().port}`,async({page})=>{
  const frames=[];
  for (const state of states) for(const blink of [false,true]) {
    const expression=`(async()=>{
      const state=${JSON.stringify(state)}, blink=${blink};
      const link=document.getElementById('link');if(!link.checked)link.click();
      const menu=document.getElementById('scene');menu.value=state==='offline'?'idle':state;menu.dispatchEvent(new Event('change'));
      if(state==='offline')link.click();
      document.getElementById('reduced').checked=true;
      await new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
      if(blink){document.getElementById('eyes').setAttribute('visibility','hidden');document.getElementById('closed-eyes').setAttribute('visibility','visible');}
      const original=document.getElementById('ghost'), clone=original.cloneNode(true);
      const sourceNodes=[original,...original.querySelectorAll('*')], clonedNodes=[clone,...clone.querySelectorAll('*')];
      const properties=['fill','stroke','stroke-width','stroke-linecap','stroke-linejoin','opacity','visibility'];
      for(let i=0;i<sourceNodes.length;i++){
        const style=getComputedStyle(sourceNodes[i]);
        for(const property of properties){let value=style.getPropertyValue(property);if(value.includes('#ghost-color'))value='url(#ghost-color)';clonedNodes[i].setAttribute(property,value);}
      }
      clone.removeAttribute('transform');
      const svg=document.createElementNS('http://www.w3.org/2000/svg','svg');
      svg.setAttribute('width','280');svg.setAttribute('height','280');svg.setAttribute('viewBox','-140 -130 280 280');
      const defs=document.createElementNS(svg.namespaceURI,'defs');defs.append(document.getElementById('ghost-color').cloneNode(true));svg.append(defs,clone);
      const serialized=new XMLSerializer().serializeToString(svg);
      const parseError=new DOMParser().parseFromString(serialized,'image/svg+xml').querySelector('parsererror');
      if(parseError)throw new Error(parseError.textContent);
      const objectURL=URL.createObjectURL(new Blob([serialized],{type:'image/svg+xml'}));
      try {
        const image=new Image();await new Promise((resolve,reject)=>{image.onload=resolve;image.onerror=()=>reject(new Error('Character SVG rasterization failed'));image.src=objectURL;});
        const canvas=document.createElement('canvas');canvas.width=canvas.height=280;
        const context=canvas.getContext('2d',{willReadFrequently:true});context.drawImage(image,0,0);
        const pixels=context.getImageData(0,0,280,280).data;
        const swatch=document.createElement('canvas').getContext('2d');swatch.fillStyle=getComputedStyle(document.querySelector('.screen-bg')).fill;swatch.fillRect(0,0,1,1);
        const background=Array.from(swatch.getImageData(0,0,1,1).data).slice(0,3);
        let bytes='';for(let i=0;i<pixels.length;i+=8192)bytes+=String.fromCharCode(...pixels.subarray(i,i+8192));
        return {rgba:btoa(bytes),background};
      } finally {URL.revokeObjectURL(objectURL);}
    })()`;
    const response=await page('Runtime.evaluate',{expression,awaitPromise:true,returnByValue:true});
    if(response.exceptionDetails)throw new Error(response.exceptionDetails.exception?.description??'Export failed');
    const {rgba,background}=response.result.value;
    const pixels=Buffer.from(rgba,'base64');if(pixels.length!==280*280*4)throw new Error('Unexpected image size');
    const runs=[], decoded=Buffer.alloc(280*280*3);let visible=0;
    for(let y=0;y<280;y++){
      let last=-1,count=0;
      for(let x=0;x<280;x++){
        const i=(y*280+x)*4,a=pixels[i+3];
        const rgb=background.map((bg,c)=>Math.round((pixels[i+c]*a+bg*(255-a))/255));
        const color=a===0?-1:((rgb[0]>>3)<<11)|((rgb[1]>>2)<<5)|(rgb[2]>>3);
        if(a){visible++;const at=(y*280+x)*3;decoded[at]=1;decoded[at+1]=color&255;decoded[at+2]=color>>8;}
        if(x && color!==last){runs.push([count|(last===-1?0x8000:0),last===-1?0:last]);count=0;}
        count++;last=color;
      }
      runs.push([count|(last===-1?0x8000:0),last===-1?0:last]);
    }
    if(visible<10000 || visible>60000)throw new Error('Unexpected character coverage');
    // Decoder round-trip: every run must stay in a row and cover the image exactly.
    let position=0;
    for(const [tag] of runs){const n=tag&0x7fff;if(!n || (position%280)+n>280)throw new Error('Invalid run');position+=n;}
    if(position!==280*280)throw new Error('Incomplete export');
    frames.push({state,blink,background,visible,runs,rgbaSha256:sha(pixels),decodedSha256:sha(decoded)});
  }
  return frames;
},90000); } finally {await new Promise(resolve=>server.close(resolve));}
const unique=[],rows=[];
for(const frame of result){
  const fingerprint=sha(JSON.stringify(frame.runs));let index=unique.findIndex(item=>item.fingerprint===fingerprint);
  if(index<0){index=unique.length;unique.push({fingerprint,runs:frame.runs});}
  rows.push({state:frame.state,blink:frame.blink,array:index,rgbaSha256:frame.rgbaSha256,decodedSha256:frame.decodedSha256,visiblePixels:frame.visible,background:frame.background});
}
let header='// Generated from original approved preview artwork. Do not hand-edit.\n#pragma once\n#include <stdint.h>\n#include <stddef.h>\nnamespace watcher_art {\nstruct Run { uint16_t tag; uint16_t color; }; // high tag bit = transparent; low bits = length\nstruct Sprite { const Run* runs; size_t count; };\nconstexpr unsigned width=280, height=280, origin_x=140, origin_y=130;\n';
for(const [i,item] of unique.entries()){
 header+=`inline constexpr Run image_${i}[] = {\n`;
 for(let n=0;n<item.runs.length;n+=12)header+='  '+item.runs.slice(n,n+12).map(([tag,color])=>`{${tag},${color}}`).join(',')+',\n';
 header+='};\n';
}
header+='// States: '+states.join(', ')+'; each has normal then blink.\ninline constexpr Sprite sprites[][2] = {\n';
for(let n=0;n<rows.length;n+=2)header+='  {'+rows.slice(n,n+2).map(row=>`{image_${row.array},sizeof(image_${row.array})/sizeof(Run)}`).join(',')+'},\n';
header+='};\n} // namespace watcher_art\n';
await mkdir(destination,{recursive:true});
await writeFile(new URL('ghost.generated.h',destination),header);
const sourceHashes={};for(const file of ['index.html','style.css','screen.mjs','preview.mjs','export-ghost.mjs','chrome-harness.mjs','server-core.mjs'])sourceHashes[file]=sha(await readFile(new URL(file,import.meta.url)));
const manifest={schema:1,width:280,height:280,origin:[140,130],format:'row-bounded RGB565 runs; tag high-bit transparent',states,sourceHashes,frames:rows,uniqueImages:unique.length,binaryRunBytes:unique.reduce((n,item)=>n+item.runs.length*4,0),headerSha256:sha(header),limitations:'Browser-generated artwork, not device or SPI performance validation. Partially transparent pixels are preblended to the fixed screen background.'};
await writeFile(new URL('ghost.manifest.json',destination),JSON.stringify(manifest,null,2)+'\n');
console.log(JSON.stringify({output:fileURLToPath(destination),uniqueImages:manifest.uniqueImages,binaryRunBytes:manifest.binaryRunBytes,headerSha256:manifest.headerSha256},null,2));
