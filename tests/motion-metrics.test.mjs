import test from 'node:test';
import assert from 'node:assert/strict';
import {motionMetricKeys,motionMetrics,validMotionMetrics,MotionMetricsReader} from '../host/motion-metrics.mjs';
import {crc32} from '../host/wire.mjs';
const sample=Object.fromEntries(motionMetricKeys.map(k=>[k,k==='fault'?0:k==='boot'?7:1]));
const line=(values=sample)=>{const prefix=`R3 ${motionMetricKeys.map(k=>values[k]).join(' ')} `;return prefix+crc32(prefix);};
test('R3 canonical bounded counters distinguish transfers from reused publications',()=>{
 assert.deepEqual(motionMetrics(line()),sample);
 const max=Object.fromEntries(motionMetricKeys.map(k=>[k,k==='fault'?7:0xffffffff]));assert.ok(line(max).length<192);assert.deepEqual(motionMetrics(line(max)),max);
 assert.equal(validMotionMetrics({...sample,secret:'no'}),false);
 for(const bad of [line()+'\n',line()+' ',line().replace('R3 7','R3 07'),line({...sample,fault:8}),line({...sample,boot:0}),line({...sample,sequence:0}),line({...sample,frames:0x100000000}),line({...sample,frames:-1}),line({...sample,frames:1.5}),line().replace('R3','D2'),line().replace('R3 7','R3 8')])assert.equal(motionMetrics(bad),null,bad);
});
test('R3 is boot-bound, replay-rejecting, expiring and resets without liveness authority',()=>{
 const reader=new MotionMetricsReader();assert.equal(reader.snapshot(7,0),null);
 assert.equal(reader.receive(line(),8,10),false);assert.equal(reader.receive(line(),7,10),true);
 assert.equal(reader.receive(line(),7,20),false);assert.equal(reader.receive(line({...sample,sequence:2}),7,9),false);
 assert.deepEqual(reader.snapshot(7,20),{...sample,ageMs:10});assert.equal(reader.snapshot(8,20),null);assert.equal(reader.snapshot(7,9),null);assert.equal(reader.snapshot(7,10011),null);
 assert.equal(reader.receive(line({...sample,sequence:2}),7,10012),true);assert.equal(reader.snapshot(7,10012).sequence,2);
 reader.reset();assert.equal(reader.snapshot(7,10012),null);assert.equal(reader.receive(line({...sample,boot:8}),8,10013),true);
});
