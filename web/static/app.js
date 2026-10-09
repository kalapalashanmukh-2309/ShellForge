'use strict';
const $ = id => document.getElementById(id);
const local = ['127.0.0.1', 'localhost'].includes(location.hostname);
let endpoint = local ? location.origin : 'http://127.0.0.1:8765';
let token = new URLSearchParams(location.hash.slice(1)).get('token') || '';
if (token) history.replaceState(null, '', location.pathname + location.search);
let connected = false, cursor = 0, session = 0, pollGeneration = 0, inputQueue = Promise.resolve();
const terminal = new Terminal({cursorBlink:true, fontSize:14, fontFamily:'Menlo, Consolas, monospace', lineHeight:1.25, scrollback:5000, convertEol:false, theme:{background:'#0b100e',foreground:'#d8eee0',cursor:'#6af0b2',selectionBackground:'#326446',black:'#172019',red:'#ff8f83',green:'#6af0b2',yellow:'#e0cb82',blue:'#8db9ff',magenta:'#c8a4ed',cyan:'#8dd8dd',white:'#eaf3ed'}});
const fit = new FitAddon.FitAddon();
terminal.loadAddon(fit);
terminal.open($('terminal'));
fit.fit();
terminal.writeln('\x1b[38;2;106;240;178mShellForge / native terminal\x1b[0m\r\n');
terminal.writeln('Connect to your local server to start a real shell session.');
terminal.writeln('Your commands will execute on that computer.\r\n');
terminal.writeln('  1. Run make web in the project folder');
terminal.writeln('  2. Open the printed link or enter its access token');
$('endpoint').value = endpoint;
$('origin-command').textContent = 'python3 web/server.py --allow-origin ' + location.origin;
function notify(text) { $('notice').textContent=text; $('notice').style.display='block'; clearTimeout(notify.timer); notify.timer=setTimeout(()=>$('notice').style.display='none',4500); }
function connectionState(value, message) {
  connected=value;
  $('connection-label').textContent=value?'Connected':'Not connected';
  $('connection-dot').classList.toggle('online',value);
  $('interrupt').disabled=!value; $('disconnect').disabled=!value;
  $('start-button').textContent=value?'Focus terminal':'Connect terminal';
  $('terminal-state').textContent=message || (value?'Live session · Ctrl+C to interrupt · Ctrl+D to exit':'Connect to your local ShellForge server to begin.');
}
async function api(path, data) {
  const response=await fetch(endpoint+path,{method:data===undefined?'GET':'POST',headers:{'Authorization':'Bearer '+token,...(data===undefined?{}:{'Content-Type':'application/json'})},body:data===undefined?undefined:JSON.stringify(data),signal:AbortSignal.timeout(7000),credentials:'omit',cache:'no-store'});
  const result=await response.json();
  if(!response.ok) throw new Error(result.error || 'The server could not complete the request.');
  return result;
}
function openConnection() { $('token').value=token; if (!$('connection-dialog').open) $('connection-dialog').showModal(); }
async function start() {
  $('connection-error').textContent='';
  try {
    const url=new URL($('endpoint').value.trim());
    if(!['http:','https:'].includes(url.protocol) || url.username || url.password || url.search || url.hash || !['','/'].includes(url.pathname)) throw new Error('Enter an HTTP or HTTPS server origin without a path.');
    endpoint=url.origin; token=$('token').value.trim();
    if(!token) throw new Error('Enter your local server access token.');
    const info=await api('/api/start',{});
    cursor=0; session=info.session; terminal.reset();
    $('session-label').textContent='session '+info.session+' / pid '+info.pid;
    $('session-label').title=info.workspace;
    connectionState(true);
    $('connection-dialog').close();
    terminal.focus();
    await resize();
    poll(++pollGeneration);
  } catch(error) { connectionState(false); openConnection(); $('connection-error').textContent=error instanceof TypeError?'Could not reach the server. Check make web, the allowed origin, and your browser’s local-network permission.':error.message; }
}
async function poll(generation) {
  while(connected && generation===pollGeneration) {
    try {
      const result=await api('/api/output?cursor='+cursor+'&session='+session);
      if(generation!==pollGeneration) return;
      if(result.truncated) terminal.writeln('\r\n[Earlier output was trimmed.]');
      if(result.output) terminal.write(result.output);
      cursor=result.cursor; session=result.session;
      if(!result.running) { connectionState(false,'Session ended. Connect again to start a new shell.'); return; }
    } catch(error) {
      if(generation!==pollGeneration) return;
      connectionState(false,'Connection lost. Reconnect to resume the session.');
      notify(error.message); return;
    }
  }
}
function send(data) {
  if(!connected) { openConnection(); return Promise.resolve(); }
  inputQueue=inputQueue.then(async()=>{
    for(let i=0;i<data.length;i+=1024) await api('/api/input',{data:data.slice(i,i+1024)});
  }).catch(error=>{notify(error.message); connectionState(false,'Connection lost. Reconnect to resume.');});
  return inputQueue;
}
terminal.onData(data=>send(data));
async function resize() {
  fit.fit();
  if(connected) { try { await api('/api/resize',{rows:Math.min(200,Math.max(2,terminal.rows)),cols:Math.min(400,Math.max(10,terminal.cols))}); } catch(error) { notify(error.message); } }
}
let resizeTimer;
new ResizeObserver(()=>{clearTimeout(resizeTimer); resizeTimer=setTimeout(resize,100);}).observe($('terminal'));
$('connect-form').addEventListener('submit',event=>{event.preventDefault(); start();});
$('connection-button').onclick=openConnection;
$('start-button').onclick=()=>connected?terminal.focus():openConnection();
$('guide-button').onclick=()=>$('guide-dialog').showModal();
document.querySelectorAll('.close-dialog').forEach(button=>button.onclick=()=>button.closest('dialog').close());
$('clear').onclick=()=>{terminal.clear(); terminal.focus();};
$('interrupt').onclick=()=>{send('\x03'); terminal.focus();};
$('disconnect').onclick=async()=>{try {await api('/api/stop',{}); ++pollGeneration; connectionState(false,'Session ended. Files remain in your workspace.'); terminal.writeln('\r\n[Session ended]');} catch(error) {notify(error.message);}};
document.querySelectorAll('.example').forEach(button=>button.onclick=()=>{
  if(!connected) {openConnection(); return;}
  send('\x15'+button.dataset.command); terminal.focus(); notify('Example inserted. Press Enter to run.');
});
// Optional browser-agent tools use the same visible terminal and authentication.
if(document.modelContext && typeof document.modelContext.registerTool==='function') {
  const lifecycle = new AbortController();
  const tool = {name:'shellforge_insert_command',title:'Insert a shell command',description:'Insert one command into the connected terminal without executing it. The user presses Enter.',inputSchema:{type:'object',properties:{command:{type:'string'}},required:['command'],additionalProperties:false},annotations:{readOnlyHint:false,untrustedContentHint:false},execute:async(input)=>{
    if(!input || typeof input.command !== 'string' || input.command.length > 4096 || /[\r\n\x00-\x1f\x7f]/.test(input.command)) throw new Error('Use one line without control characters (maximum 4096 characters).');
    if(!connected) throw new Error('Connect the terminal first.');
    await send('\x15'+input.command);terminal.focus();
    return {status:'inserted',message:'Press Enter in the terminal to run the command.'};
  }};
  try { Promise.resolve(document.modelContext.registerTool(tool,{signal:lifecycle.signal})).catch(()=>{}); } catch(error) { /* Optional capability is unavailable. */ }
  window.addEventListener('pagehide',()=>lifecycle.abort(),{once:true});
}
if(token) { $('token').value=token; start(); }
