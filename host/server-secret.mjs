// A deliberately small secret field: no editor history, kill ring, clipboard,
// external editor, raw-value rendering, tool results or session entries.
export function secretField({title,maxBytes,valid,keybindings,done,render=()=>{},signal}){
 let value='',paste=null,pasteInvalid=false,notice='',finished=false;
 const finish=result=>{if(finished)return;finished=true;value='';paste=null;signal?.removeEventListener('abort',abort);done(result);};
 const abort=()=>finish(undefined);
 const append=text=>{
  if(/[\p{Cc}\p{Cf}\p{Cs}\p{Zl}\p{Zp}]/u.test(text)||Buffer.byteLength(value+text)>maxBytes){notice='Input rejected. Clear and re-enter the value.';return;}
  value+=text;notice='';
 };
 const component={
  render(width){return [title,'*'.repeat(Array.from(value).length)||'(hidden)',notice||'Enter: continue | Esc: cancel | Backspace: delete | Ctrl+U: clear'].map(line=>line.slice(0,Math.max(0,width)));},
  invalidate(){},
  handleInput(data){
   if(finished||typeof data!=='string')return;
   if(paste!==null||data.startsWith('\x1b[200~')){
    let chunk=data;
    if(paste===null){paste='';pasteInvalid=false;chunk=chunk.slice(6);}
    const end=chunk.indexOf('\x1b[201~');
    const text=end<0?chunk:chunk.slice(0,end);
    if(Buffer.byteLength(paste)+Buffer.byteLength(text)>maxBytes)pasteInvalid=true;
    if(!pasteInvalid)paste+=text;
    if(end>=0){if(pasteInvalid)notice='Paste too long. Nothing was inserted.';else append(paste);paste=null;}
   }else if(keybindings.matches(data,'tui.select.cancel'))finish(undefined);
   else if(keybindings.matches(data,'tui.input.submit')){
    if(valid(value)&&!notice)finish(value);else notice='Invalid value. Clear and re-enter it.';
   }else if(keybindings.matches(data,'tui.editor.deleteCharBackward')){value=Array.from(value).slice(0,-1).join('');notice='';}
   else if(keybindings.matches(data,'tui.editor.deleteToLineStart')){value='';notice='';}
   else if(!/[\x00-\x1f\x7f-\x9f]/u.test(data))append(data);
   render();
  },
  dispose(){value='';paste=null;signal?.removeEventListener('abort',abort);},
 };
 signal?.addEventListener('abort',abort,{once:true});
 if(signal?.aborted)queueMicrotask(abort);
 return component;
}
export function promptSecret(ctx,title,{maxBytes,valid,signal}){
 if(ctx.mode!=='tui')return Promise.resolve(undefined);
 return ctx.ui.custom((tui,_theme,keybindings,done)=>secretField({title,maxBytes,valid,keybindings,done,signal,render:()=>tui.requestRender()}));
}
