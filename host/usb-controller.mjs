// Pure orchestration around the attention model + durable ledger; no port access.
import { encodeFrame, encodeChime, encodeAudioInit, encodeAudioChime, deviceStatus } from './wire.mjs';
import { ghostFamilyStatus, GhostContext, rosterIdentity } from './ghost-wire.mjs';
import { CompletionChime } from './completion-chime.mjs';
import { deliveryKey } from './delivery-ledger.mjs';
import { SettingsSync } from './settings-sync.mjs';
import { SessionCycle } from './session-cycle.mjs';
export class UsbController {
  #model; #ledger; #boot=null; #retired=new Set(); #seq=0; #baseline=0; #ackedSeq=0;
  #now=0; #progressAt=0; #ready=false; #fault=null; #frames=new Map(); #buttonAck=0;
  #alertCounter=0; #acceptedAlert=0; #capabilities=0; #rendered=0; #pending=null; #waiting=new Map(); #sentAlerts=new Map(); #lastEmission=-1000;
  #chime=new CompletionChime();
  #settings=null;#settingsSync=null;#cycle;
  observeSettings(line,now){
    if(!this.#settingsSync||!this.readyAt(now))return false;
    const accepted=this.#settingsSync.receive(line,{boot:this.#boot,seq:this.#ackedSeq,now,ready:true});
    if(accepted)this.#model.interaction(this.#settingsSync.state(now).held);
    return accepted;
  }
  settingsState(now=this.#now){return this.#settingsSync?.state(now)??null;}
  #ghostContext=new GhostContext();
  #family=false;#familyStatusAt=null;
  // Called only on the active, freshly receiving transport. No authority,
  // activity clock, alert ACK, readiness or voice state can be changed here.
  observeFamily(line,now=this.#now){
    const status=ghostFamilyStatus(line);
    if(this.#fault||this.#familyStatusAt===null||!Number.isSafeInteger(now)||now<this.#now||now-this.#familyStatusAt>1500||!status||status.boot!==this.#boot||status.seq!==this.#ackedSeq||!(this.#capabilities&8))return false;
    this.#family=true;return true;
  }
  #timingDiagnostics=false;#linkGeneration=0;#lastDiagnosticRoster=null;#diagnosticRosterGeneration=0;
  #paging={observed:0,applied:0,noChange:0,unknownFrame:0,selectedMismatch:0,selectionEpoch:0,rosterEpoch:0,rosterIdentity:0};
  #countPaging(key){if(this.#timingDiagnostics)this.#paging[key]=Math.min(0xffffffff,this.#paging[key]+1);}
  #pagingAdmission(shown,current){
    if(!this.#timingDiagnostics)return;
    if(shown.sessionId!==current.selectedSessionId)this.#countPaging('selectedMismatch');
    else if(shown.selectionEpoch!==this.#model.selection().epoch)this.#countPaging('selectionEpoch');
    else if(shown.rosterEpoch!==this.#model.rosterEpoch())this.#countPaging('rosterEpoch');
    else if(shown.roster!==rosterIdentity(current))this.#countPaging('rosterIdentity');
  }
  pagingDiagnostics(){
    if(!this.#timingDiagnostics)return null;
    // Map opaque authority symbols to anonymous observed generations. Never
    // replace the model's symbols or infer stability from session count alone.
    const roster=this.#model.rosterEpoch();
    if(roster!==this.#lastDiagnosticRoster){this.#lastDiagnosticRoster=roster;this.#diagnosticRosterGeneration=Math.min(0xffffffff,this.#diagnosticRosterGeneration+1);}
    return Object.freeze({...this.#paging,rosterGeneration:this.#diagnosticRosterGeneration,linkGeneration:this.#linkGeneration});
  }
  #audioInitProbe=false;#audioInitSent=false;
  #audioChimeProbe=false;#audioChimeSent=false;#audioChimeSuppressed=false;
  constructor(model,ledger,options={}) {
    if(Object.hasOwn(options,'completionChime'))throw new Error('completionChime is retired; use watch Settings Sounds and Vibration');
    const {reduced=false,audioInitProbe=false,audioChimeProbe=false,timingDiagnostics=false,settings=null}=options;
    if(audioChimeProbe && audioInitProbe)throw new Error('Audible probe must run alone');
    this.#audioInitProbe=!!audioInitProbe;this.#audioChimeProbe=!!audioChimeProbe;this.#timingDiagnostics=!!timingDiagnostics;
    this.#model=model;this.#ledger=ledger;this.reduced=!!reduced;
    this.#settings=settings;this.#settingsSync=settings?new SettingsSync(settings):null;this.#cycle=new SessionCycle(model);
    model.connected(false);model.mute(ledger.muted);
  }
  #time(now){if(!Number.isSafeInteger(now)||now<this.#now)throw new TypeError('Invalid controller clock');this.#now=now;if(!this.#model.time(now))throw new Error('Model clock rejected');}
  #restoreSelected(){
    const snapshot=this.#model.snapshot(),row=snapshot.sessions.find(item=>item.id===snapshot.selectedSessionId);
    if(row?.selected && !row.selected.acknowledged && this.#ledger.acknowledged(row.id,row.selected.id))this.#model.acknowledge(row.id,row.selected.id);
  }
  disconnected(){
    if(this.#timingDiagnostics)this.#linkGeneration=Math.min(0xffffffff,this.#linkGeneration+1);
    this.#chime.reset();this.#family=false;this.#familyStatusAt=null;
    this.#ready=false;this.#model.connected(false);
    this.#settingsSync?.reset(this.#boot);this.#cycle.reset();this.#model.interaction(!!this.#settingsSync);
    this.#frames.clear();this.#pending=null;this.#waiting.clear();this.#sentAlerts.clear();
    this.#baseline=this.#seq;
  }
  #fail(error){this.disconnected();this.#fault=String(error?.message??error);}
  observe(line,now){
    if(this.#fault)throw new Error(this.#fault);
    try{return this.#observe(line,now);}catch(error){this.#fail(error);throw error;}
  }
  #observe(line,now){
    this.#time(now);
    const status=deviceStatus(line);if(!status||this.#retired.has(status.boot))return false;
    if(status.boot!==this.#boot) {
      // Keep replay tombstones; never evict old boots to make a reconnect work.
      // LAN flaps need substantially more headroom than the old 16-boot USB cap.
      if(this.#retired.size>=4096)throw new Error('Device generation capacity exhausted');
      if(this.#boot!==null)this.#retired.add(this.#boot);
      this.disconnected();this.#familyStatusAt=now;this.#boot=status.boot;this.#settingsSync?.reset(this.#boot);this.#seq=status.seq;this.#baseline=status.seq;this.#ackedSeq=status.seq;
      this.#capabilities=status.capabilities;this.#rendered=status.rendered;
      this.#buttonAck=status.buttonId;this.#alertCounter=status.alert;this.#acceptedAlert=status.alert;this.#progressAt=now;
      return true; // two-way proof still required before any intent can issue
    }
    if(status.seq<this.#ackedSeq || status.seq>this.#seq || status.alert>this.#alertCounter)return false;
    this.#familyStatusAt=now;
    this.#capabilities=status.capabilities;this.#rendered=Math.max(this.#rendered,status.rendered);
    if(status.seq>this.#ackedSeq && this.#frames.has(status.seq)) {
      this.#ackedSeq=status.seq;this.#progressAt=now;
      if(status.seq>this.#baseline){this.#ready=true;this.#model.connected(true);}
    }
    if(status.alert>this.#acceptedAlert) {
      const sent=this.#sentAlerts.get(status.alert);
      if(sent)this.#ledger.accepted(sent);
      this.#acceptedAlert=status.alert;
      if(this.#pending && status.alert>=this.#pending.id)this.#pending=null;
      for(const id of this.#sentAlerts.keys())if(id<=status.alert)this.#sentAlerts.delete(id);
    }
    if(this.#ready && status.buttonKind && status.buttonId>this.#buttonAck) {
      const shown=this.#frames.get(status.buttonFrame),paging=status.buttonKind===1||status.buttonKind===2;
      if(paging){this.#countPaging('observed');if(!shown||status.buttonFrame>status.seq)this.#countPaging('unknownFrame');}
      // Consume stale events too: never reinterpret them against a different item.
      this.#buttonAck=status.buttonId;
      if(shown && status.buttonFrame<=status.seq) {
        const current=this.#model.snapshot();
        if(paging)this.#pagingAdmission(shown,current);
        if(status.buttonKind===4){const value=!current.muted;this.#ledger.mute(value);this.#model.mute(value);}
        else if(shown.sessionId===current.selectedSessionId) {
          // Paging must also reject selection ABA, including a swipe that began
          // before the user navigated away and back. ACK/mute remain separate.
          if(shown.selectionEpoch===this.#model.selection().epoch&&shown.rosterEpoch===this.#model.rosterEpoch()&&shown.roster===rosterIdentity(current)){
            if(status.buttonKind===1){this.#model.page(-1);this.#countPaging(current.sessions.length>1?'applied':'noChange');}
            if(status.buttonKind===2){this.#model.page(1);this.#countPaging(current.sessions.length>1?'applied':'noChange');}
          }
          if(status.buttonKind===3 && shown.itemId) {
            const row=current.sessions.find(item=>item.id===shown.sessionId);
            if(row?.selected?.id===shown.itemId && !row.selected.acknowledged) {
              this.#ledger.acknowledge(shown.sessionId,shown.itemId);
              this.#model.acknowledge(shown.sessionId,shown.itemId);
            }
          }
        }
      }
    }
    return true;
  }
  tick(now){
    if(this.#fault)throw new Error(this.#fault);
    try{return this.#tick(now);}catch(error){this.#fail(error);throw error;}
  }
  #tick(now){
    this.#time(now);
    if(this.#ready && now-this.#progressAt>1500)this.disconnected();
    const preferences=this.#settings?.snapshot(),settingsState=this.#settingsSync?.state(now);
    const synchronized=!!settingsState?.supported&&settingsState.interactionFresh&&!settingsState.failed;
    this.#model.interaction(!!settingsState&&settingsState.held);
    this.#cycle.tick(now,!!preferences?.cycleSessions&&synchronized&&!settingsState.held&&this.#ready);
    this.#restoreSelected();
    this.#ledger.claim(this.#model.notificationReceipts().filter(item=>item.notification==='suppressed'),'suppressed');
    let keys=[];
    const motor=!!(this.#capabilities&1)&&!(this.#capabilities&2),speaker=!!(this.#capabilities&4);
    const outputMask=preferences?(synchronized?((preferences.sounds&&speaker?1:0)|(preferences.vibration&&motor?2:0)):0):(motor?2:0);
    const haptics=outputMask!==0; // Historical name: ledger now claims any permitted notification output.
    const intents=this.#model.flush(batch=>{keys=this.#ledger.claim(batch,haptics?'claimed':'suppressed');});
    const fresh=new Set(keys);
    for(const item of intents){const key=deliveryKey(item.sessionId,item.itemId);if(haptics && fresh.has(key))this.#waiting.set(key,item);}
    const receipts=this.#model.notificationReceipts();
    const unresolved=new Set(receipts.filter(item=>item.notification==='issued'&&!item.acknowledged).map(item=>deliveryKey(item.sessionId,item.itemId)));
    const live=new Set(receipts.filter(item=>item.notification==='issued'&&item.confirmed&&!item.acknowledged&&item.available).map(item=>deliveryKey(item.sessionId,item.itemId)));
    // Unsent work waits through temporary uncertainty; only terminal/ACK/suppression retires it.
    for(const key of this.#waiting.keys())if(!unresolved.has(key))this.#waiting.delete(key);
    const snapshot=this.#model.snapshot();
    const chime=this.#chime.take(snapshot,this.#ready&&(preferences?outputMask!==0:false));
    if(snapshot.muted || !haptics){this.#waiting.clear();this.#pending=null;}
    if(this.#pending && !this.#pending.keys.some(key=>live.has(key)))this.#pending=null;
    const eligible=[...this.#waiting.keys()].filter(key=>live.has(key));
    if(this.#pending && now-this.#pending.started>1500)throw new Error('Device alert acceptance timed out');
    if(this.#ready && !this.#pending && eligible.length && now-this.#lastEmission>=250) {
      if(this.#alertCounter===0xffffffff)throw new Error('Device alert counter exhausted');
      this.#pending={id:++this.#alertCounter,keys:eligible,started:now};
      for(const key of eligible)this.#waiting.delete(key);
      this.#lastEmission=now;
      this.#sentAlerts.set(this.#pending.id,this.#pending.keys);
      if(this.#sentAlerts.size>32)throw new Error('Unreconciled delivery capacity exhausted');
    }
    if(this.#boot===null)return 'Q1\n';
    if(this.#seq===0xffffffff)throw new Error('Device sequence exhausted');
    const seq=++this.#seq,row=snapshot.sessions.find(item=>item.id===snapshot.selectedSessionId);
    const line=encodeFrame(snapshot,{boot:this.#boot,seq,alert:this.#pending?.id??this.#acceptedAlert,eventAck:this.#buttonAck,reduced:this.reduced||!!preferences&&!preferences.animations});
    this.#frames.set(seq,{sessionId:row?.id??null,itemId:row?.selected?.id??null,selectionEpoch:this.#model.selection().epoch,roster:rosterIdentity(snapshot),rosterEpoch:this.#model.rosterEpoch()});
    if(this.#frames.size>128)this.#frames.delete(this.#frames.keys().next().value);
    let probe='';
    if(this.#audioInitProbe && !this.#audioInitSent && this.#ready && snapshot.sessions.length && !snapshot.muted){
      this.#audioInitSent=true; // Never retransmit or repeat across device reconnects.
      probe=encodeAudioInit(this.#boot,seq);
    }
    if(this.#audioChimeProbe && !this.#audioChimeSent && !this.#audioChimeSuppressed && this.#ready && snapshot.sessions.length){
      // Muted tests are consumed, never armed for a surprise sound on later unmute.
      if(snapshot.muted)this.#audioChimeSuppressed=true;
      else{this.#audioChimeSent=true;probe=encodeAudioChime(this.#boot,seq);}
    }
    const settingsLine=this.#settingsSync?.frame({boot:this.#boot,seq,now,ready:this.#ready},{forced:this.reduced})??'';
    return line+settingsLine+((this.#capabilities&8)?this.#ghostContext.encode(snapshot,this.#model.selection().epoch,{boot:this.#boot,seq},this.#model.rosterEpoch())+this.#ghostContext.rows(snapshot,{boot:this.#boot,seq,family:this.#family}):'')+probe+(chime?encodeChime(this.#boot,seq):'');
  }
  // Fresh two-way transport progress; diagnostics do not renew this clock.
  readyAt(now){return !this.#fault&&this.#ready&&Number.isSafeInteger(now)&&now>=this.#now&&now-this.#progressAt<=1500;}
  status(){return Object.freeze({audioChimeSuppressed:this.#audioChimeSuppressed,audioChimeSent:this.#audioChimeSent,audioInitSent:this.#audioInitSent,boot:this.#boot,ready:this.#ready,seq:this.#seq,pending:this.#pending!==null,queued:this.#waiting.size,buttonAck:this.#buttonAck,rendered:this.#rendered,capabilities:this.#capabilities,fault:this.#fault});}
}
