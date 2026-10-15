// Local preview only; the ordinary preview does not permit blob images.
import { createPreviewServer } from './server-core.mjs';
const server=createPreviewServer();
server.listen(0,'127.0.0.1',()=>{
  const url=`http://127.0.0.1:${server.address().port}`;
  console.log(url);
  process.send?.({url,pid:process.pid});
});
process.on('SIGTERM',()=>server.close());
