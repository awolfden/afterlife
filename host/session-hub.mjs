// Bounded metadata-only ingress. No reverse agent-control channel.
const opaque=v=>typeof v==='string'&&/^[a-f0-9]{64}$/.test(v);
const plain=v=>v&&typeof v==='object'&&!Array.isArray(v);
const exact=(value,keys)=>plain(value)&&Object.keys(value).sort().join(',')===keys.split(',').sort().join(',');
const states=new Set(['idle','thinking','working','completed','failed']);
export class SessionHub {
  #model; #owners=new Map();
  constructor(model){this.#model=model;}
  attach(started=0){
    if(!Number.isSafeInteger(started)||started<0)throw new TypeError('Invalid ingress clock');
    let id=null,seq=0,closed=false,observed=started;
    const token={};
    const disconnect=()=>{
      if(closed)return;closed=true;
      const owner=this.#owners.get(id);
      if(!owner||owner.token!==token)return;
      for(const item of owner.open)this.#model.uncertain(id,item);
      this.#model.status(id,{state:owner.state,activeChildren:owner.children,available:false});
      this.#model.supervisorSync(id,false);owner.token=null;
    };
    const receive=(message,now)=>{
      if(closed||!Number.isSafeInteger(now)||now<observed||!plain(message)||message.v!==1||!Number.isSafeInteger(message.seq)||message.seq<=seq)return false;
      if(!this.#model.time(now))return false;
      if(id===null){
        if(!exact(message,'v,seq,op,id')||message.op!=='hello'||!opaque(message.id))return false;
        const prior=this.#owners.get(message.id);
        if(prior?.token || !this.#model.register(message.id))return false;
        id=message.id;seq=message.seq;observed=now;
        this.#owners.set(id,{token,state:prior?.state??'idle',children:prior?.children??0,open:prior?.open??new Set(),rejected:prior?.rejected??false});
        this.#model.supervisorSync(id,false);return true;
      }
      const owner=this.#owners.get(id);if(owner?.token!==token)return false;
      let valid=false;
      switch(message.op){
        case 'identity':
          if(exact(message,'v,seq,op,identity')){
            // Optional display metadata must never block authoritative alert replay.
            valid=true;if(!this.#model.identity(id,message.identity))this.#model.identity(id,null);
          }
          break;
        case 'status':
          if(exact(message,'v,seq,op,state,children,available')&&states.has(message.state)&&Number.isInteger(message.children)&&message.children>=0&&message.children<=256&&typeof message.available==='boolean'){
            valid=this.#model.status(id,{state:message.state,activeChildren:message.children,available:message.available});
            if(valid){owner.state=message.state;owner.children=message.children;}
          }break;
        case 'sync':
          if(exact(message,'v,seq,op,synced')&&typeof message.synced==='boolean')valid=this.#model.supervisorSync(id,message.synced&&!owner.rejected);break;
        case 'attention':
          if(exact(message,'v,seq,op,item,kind')&&opaque(message.item)&&['input','child','failure'].includes(message.kind)){
            valid=true;
            if(this.#model.attention(id,{itemId:message.item,kind:message.kind}))owner.open.add(message.item);
            else {owner.rejected=true;this.#model.supervisorSync(id,false);}
          }break;
        case 'unknown':
          if(exact(message,'v,seq,op,item,kind')&&opaque(message.item)&&['input','child','failure'].includes(message.kind)){
            valid=true;
            if(this.#model.uncertain(id,message.item,message.kind))owner.open.add(message.item);
            else {owner.rejected=true;this.#model.supervisorSync(id,false);}
          }break;
        case 'resolve':
          if(exact(message,'v,seq,op,item')&&opaque(message.item)){
            valid=this.#model.resolve(id,message.item);if(valid)owner.open.delete(message.item);
          }break;
        case 'heartbeat': valid=exact(message,'v,seq,op');break;
        // Explicit opt-out removes only the watcher subscription, never resolves agent work.
        case 'unwatch':
          if(exact(message,'v,seq,op')){seq=message.seq;closed=true;this.#owners.delete(id);this.#model.remove(id);return true;}
          break;
        // Ending/disconnecting is uncertainty, not proof that direct children terminated.
        case 'end': if(exact(message,'v,seq,op')){seq=message.seq;disconnect();return true;}break;
        default:break;
      }
      if(valid){seq=message.seq;observed=now;}return valid;
    };
    return Object.freeze({receive,disconnect,stale:now=>{if(!closed&&now-observed>5000)disconnect();},get closed(){return closed;}});
  }
}
