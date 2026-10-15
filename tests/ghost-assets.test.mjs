import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
const art=new URL('../firmware/art/',import.meta.url);
test('generated ghost matches approved preview sources and recorded header digest',async()=>{
 const manifest=JSON.parse(await readFile(new URL('ghost.manifest.json',art),'utf8'));
 assert.equal(sha(await readFile(new URL('ghost.generated.h',art))),manifest.headerSha256);
 for(const [file,hash] of Object.entries(manifest.sourceHashes))assert.equal(sha(await readFile(new URL(`../preview/${file}`,import.meta.url))),hash,file);
 assert.equal(manifest.frames.length,18);assert.equal(manifest.uniqueImages,16);
 assert.ok(manifest.binaryRunBytes<600000);
 for(const frame of manifest.frames){assert.match(frame.decodedSha256,/^[a-f0-9]{64}$/);assert.deepEqual(frame.background,[12,10,16]);}
});
