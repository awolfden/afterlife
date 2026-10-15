// Safe for global discovery: no socket, timers or telemetry until /watch.
import {registerWatcher} from '../host/pi-observer.mjs';
import {spaceIdentityEnabled} from '../host/session-identity.mjs';
import {createServerCommand} from '../host/server-command.mjs';
export default function watchCommand(pi){
 registerWatcher(pi,{autoStart:false,serverCommand:createServerCommand(pi),spaceIdentity:spaceIdentityEnabled()});
}
