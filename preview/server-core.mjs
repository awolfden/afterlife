// Fixed public-file allowlist. No device APIs, filesystem browsing or mutations.
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
const files = new Map([
  ['/', ['index.html','text/html; charset=utf-8']],
  ['/style.css',['style.css','text/css; charset=utf-8']],
  ['/preview.mjs',['preview.mjs','text/javascript; charset=utf-8']],
  ['/screen.mjs',['screen.mjs','text/javascript; charset=utf-8']],
  ['/attention.mjs',['../host/attention.mjs','text/javascript; charset=utf-8']],
]);
export function createPreviewServer({allowBlobImages=false}={}) {
  return createServer(async(req,res)=>{
    if (!/^127\.0\.0\.1:\d+$/.test(req.headers.host??'')) {res.writeHead(403).end();return;}
    if (req.method!=='GET' && req.method!=='HEAD') {res.writeHead(405,{Allow:'GET, HEAD'}).end();return;}
    const file=files.get(req.url);
    if (!file) {res.writeHead(404).end();return;}
    try {
      const body=await readFile(new URL(file[0],import.meta.url));
      res.writeHead(200,{'Content-Type':file[1],'Cache-Control':'no-store','X-Content-Type-Options':'nosniff',
        'Content-Security-Policy':`default-src 'none'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self'${allowBlobImages?' blob:':''}; connect-src 'none'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'`});
      res.end(req.method==='HEAD'?undefined:body);
    } catch {res.writeHead(500).end('Preview file unavailable');}
  });
}
