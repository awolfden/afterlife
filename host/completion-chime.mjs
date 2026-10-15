// Best-effort, at-most-once completion sound. Never queue/replay missed sounds.
export class CompletionChime {
  #seen=new Map();
  reset(){this.#seen.clear();}
  take(snapshot,enabled){
    let play=false;
    const live=new Set(snapshot.sessions.map(row=>row.id));
    for(const id of this.#seen.keys())if(!live.has(id))this.#seen.delete(id);
    for(const row of snapshot.sessions){
      const previous=this.#seen.get(row.id);
      this.#seen.set(row.id,row.completions);
      if(previous!==undefined && row.completions>previous && enabled && !snapshot.muted && row.available && row.state==='completed')play=true;
    }
    return play; // Simultaneous completions coalesce into one short chime.
  }
}
