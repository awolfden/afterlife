// Original approved characters. Expressions change faces, never body identity.
export const characters=Object.freeze(['purple','rascal','mallow','patch','wisp','ember']);
export const states=Object.freeze(['idle','thinking','working','needs-input','child-help','completed','failed','unavailable','offline']);
const faces={rascal:{x:140,y:129,dx:35,rx:10,ry:15,mouth:38},mallow:{x:142,y:133,dx:25,rx:8,ry:12,mouth:23},patch:{x:140,y:134,dx:29,rx:19,ry:23,mouth:40,white:true},wisp:{x:140,y:129,dx:27,rx:12,ry:18,mouth:29},ember:{x:141,y:143,dx:26,rx:16,ry:20,mouth:32,white:true}};
const ink='oklch(25% .038 290)',light='oklch(97% .008 290)';
const path=(d,fill='none',width=4)=>`<path d="${d}" fill="${fill}" stroke="${ink}" stroke-width="${width}" stroke-linecap="round" stroke-linejoin="round"/>`;
export function expressionSvg(source,character,state,blink=false){
 const f=faces[character];if(!f||!states.includes(state)||typeof blink!=='boolean')throw new TypeError('Invalid character expression');
 if(state==='idle'&&!blink)return source; // Preserve the approved idle drawing byte-for-byte.
 if(state==='idle'){
  const begin=source.indexOf('<g id="expression">'),end=source.indexOf('</g>',begin);
  if(begin<0||end<0)throw new Error('Missing canonical expression group');
  let eyesClosed=source.slice(begin,end).replace(/<(?:ellipse|circle)\b[^>]*>/g,element=>{
   const cx=Number(element.match(/\bcx="([^"]+)"/)?.[1]),cy=Number(element.match(/\bcy="([^"]+)"/)?.[1]);
   return Math.abs(cy-f.y)<=f.ry*1.4&&[-1,1].some(side=>Math.abs(cx-(f.x+side*f.dx))<=f.rx*1.3)?'':element;
  });
  for(const side of [-1,1])eyesClosed+=path(`M${f.x+side*f.dx-10} ${f.y}q10 -12 20 0`);
  return source.slice(0,begin)+eyesClosed+source.slice(end);
 }
 const {x,y,dx,rx,ry,mouth:m}=f,closed=blink||state==='completed'||state==='offline';
 let face='';
 for(const side of [-1,1]){
  const ex=side*dx,ey=character==='patch'&&side===1?4:0,scale=character==='patch'&&side===1?.8:1;
  if(closed)face+=path(state==='offline'?`M${ex-9} ${ey}q9 5 18 0`:`M${ex-10} ${ey}q10 -12 20 0`);
  else{
   const tall=state==='needs-input'?1.2:state==='failed'?.8:1;
   face+=`<ellipse cx="${ex}" cy="${ey}" rx="${rx*scale}" ry="${ry*scale*tall}" fill="${f.white?light:ink}"/>`;
   if(f.white)face+=`<ellipse cx="${ex+(state==='thinking'?4:0)}" cy="${ey}" rx="${rx*.45*scale}" ry="${ry*.62*scale*tall}" fill="${ink}"/>`;
   face+=`<circle cx="${ex-2+(state==='thinking'?4:0)}" cy="${ey-5}" r="2.5" fill="${light}"/>`;
  }
 }
 if(state==='thinking')face+=path(`M${-dx-10} -26q10 -5 20 0M${dx-10} -31q10 -10 20 0`, 'none',3.5);
 if(state==='needs-input')face+=path(`M${-dx-11} -29q11 -10 22 0M${dx-11} -29q11 -10 22 0`,'none',3.5);
 if(state==='child-help')face+=path(`M${-dx-11} -26l22 -7M${dx-11} -33l22 7`,'none',3.5);
 if(state==='failed')face+=path(`M${-dx-12} -29l23 10M${dx-11} -19l23 -10`,'none',4);
 if(state==='needs-input')face+=`<ellipse cx="1" cy="${m}" rx="7" ry="10" fill="${ink}"/>`;
 else if(state==='thinking')face+=path(`M0 ${m+1}q9 -7 16 -3`);
 else if(state==='unavailable')face+=path(`M-11 ${m}h22`);
 else if(state==='failed')face+=path(`M-15 ${m+5}q15 -13 30 0`);
 else if(state==='working'||state==='completed'){
  face+=path(`M-17 ${m-5}q17 10 34 0q-2 24 -17 24q-15 0 -17 -24Z`,ink,2);
  face+=`<path d="M-10 ${m+10}q10 -8 20 0q-10 13 -20 0Z" fill="oklch(73% .14 350)"/>`;
  if(character==='rascal')face+=`<path d="M-14 ${m-2}l5 9 5 -6M5 ${m+1}l5 6 5 -9" fill="${light}"/>`;
 }else face+=path(state==='offline'?`M-9 ${m}q9 3 18 0`:state==='child-help'?`M-12 ${m+3}q12 -6 24 -1`:`M-12 ${m}q12 12 24 0`);
 if(character==='mallow')face+='<ellipse cx="-44" cy="20" rx="12" ry="7" fill="oklch(79% .12 43)"/><ellipse cx="43" cy="20" rx="12" ry="7" fill="oklch(79% .12 43)"/>';
 if(character==='patch'&&['idle','child-help','unavailable'].includes(state))face+=`<path d="M-5 ${m+3}v9h8v-9" fill="${light}"/>`;
 const begin=source.indexOf('<g id="expression">'),end=source.indexOf('</g>',begin);
 if(begin<0||end<0)throw new Error('Missing canonical expression group');
 let rendered=source.slice(0,begin)+`<g id="expression" transform="translate(${x} ${y})">${face}</g>`+source.slice(end+4);
 // Match the flagship's quiet, visibly disconnected/unknown presentation.
 if(state==='unavailable'||state==='offline'){
  const body=rendered.indexOf('</defs>')+7;
  if(body<7)throw new Error('Missing canonical body boundary');
  rendered=rendered.slice(0,body)+'<g opacity=".72">'+rendered.slice(body).replace('</svg>','</g></svg>');
 }
 return rendered;
}
