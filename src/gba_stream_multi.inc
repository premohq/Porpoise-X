/* Porpoise - the GBAs' page with several GBAs at once (gba_stream.cpp: /multi). The same look,
 * buttons and settings as the single page (gba_stream_page.inc), which stays at /.
 * Included inside a namespace.
 * SPDX-License-Identifier: GPL-3.0-or-later */
constexpr const char *kMultiPage = R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no,viewport-fit=cover">
<title>GBAs - Porpoise</title>
<style>
:root{color-scheme:dark;--bg:#090e18;--key:#ffffff14;--keyline:#ffffff1f;--on:#4f8fff;--fg:#e8eef8;--dim:#a5b3c7}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent;touch-action:none;user-select:none;-webkit-user-select:none}
html,body{margin:0;width:100%;height:100%;background:var(--bg);color:var(--fg);font:13px system-ui,sans-serif;overflow:hidden}
[hidden]{display:none!important}
#app{position:fixed;inset:0;display:flex;flex-direction:column;padding:env(safe-area-inset-top) env(safe-area-inset-right) env(safe-area-inset-bottom) env(safe-area-inset-left)}
#bar{display:flex;gap:8px;align-items:center;padding:6px 10px;color:var(--dim);font-size:12px;letter-spacing:.04em}
#status{flex:1;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
#bar select,#bar button{background:#ffffff0b;color:var(--fg);border:1px solid var(--keyline);border-radius:999px;padding:4px 10px;font:inherit}
#screen{flex:1;min-height:0;display:flex;align-items:center;justify-content:center;overflow:hidden;position:relative}
canvas{image-rendering:pixelated;image-rendering:crisp-edges;background:#000;border-radius:6px;box-shadow:0 24px 80px #0006}
body.smooth canvas{image-rendering:auto}
#splash{position:absolute;inset:0;display:grid;place-items:center;background:radial-gradient(ellipse at 30% 25%,#254f7a44,transparent 55%),radial-gradient(ellipse at 80% 80%,#ae7c2526,transparent 50%),var(--bg)}
.splash{display:flex;flex-direction:column;align-items:center;gap:22px}
.mark{width:120px;height:120px;display:grid;place-items:center;border:1px solid #ffffff12;border-radius:34px;background:linear-gradient(145deg,#ffffff0b,#ffffff02);font:700 30px system-ui;color:#8bc2ff;letter-spacing:.06em}
.msg{margin:0;color:var(--dim);font:500 13px/1.5 system-ui,sans-serif;letter-spacing:.12em;text-align:center;max-width:80vw}
.dots{display:flex;gap:7px}.dots i{width:4px;height:4px;border-radius:50%;background:#8bc2ff;animation:breathe 2s ease-in-out infinite}.dots i:nth-child(2){animation-delay:.2s}.dots i:nth-child(3){animation-delay:.4s}
@keyframes breathe{0%,80%,100%{opacity:.25;transform:translateY(0)}40%{opacity:1;transform:translateY(-3px)}}
@media(prefers-reduced-motion:reduce){.dots i{animation:none;opacity:.6}}
body.nopad #pad{display:none}
body.nostatus #status{visibility:hidden}
#panel{position:fixed;top:44px;right:10px;width:min(360px,calc(100vw - 20px));max-height:calc(100vh - 60px);overflow:auto;background:#0d1424f2;border:1px solid var(--keyline);border-radius:16px;padding:14px;z-index:5;box-shadow:0 24px 80px #000a;touch-action:auto}
#panel h3{margin:4px 0 10px;font:600 13px system-ui;letter-spacing:.08em;color:var(--dim)}
#panel label{display:flex;gap:8px;align-items:center;margin:6px 0}
#panel table{width:100%;border-collapse:collapse;margin:6px 0 10px}
#panel td{padding:3px 4px}
#panel td button{width:100%;background:#ffffff0b;color:var(--fg);border:1px solid var(--keyline);border-radius:8px;padding:4px 6px;font:12px system-ui}
#panel td button.wait{background:var(--on)}
#panel .row{display:flex;gap:8px;flex-wrap:wrap;margin-top:8px}
#panel .row button{flex:1;background:#ffffff0b;color:var(--fg);border:1px solid var(--keyline);border-radius:999px;padding:6px 10px;font:12px system-ui}
#panel .danger{border-color:#ff8a8a66!important;color:#ffb3b3!important}
#pad{display:grid;grid-template-columns:1fr 1fr 1fr;align-items:center;gap:6px;padding:8px 14px 14px;max-width:760px;width:100%;margin:0 auto}
.grp{position:relative;height:150px}
.k{position:absolute;display:flex;align-items:center;justify-content:center;background:var(--key);border:1px solid var(--keyline);border-radius:14px;font-weight:600;font-size:15px;color:var(--fg)}
.k.on{background:var(--on);border-color:#9cc2ff}
#dpad .k{width:50px;height:50px}
#up{left:50px;top:0}#down{left:50px;top:100px}#left{left:0;top:50px}#right{left:100px;top:50px}
#face .k{width:62px;height:62px;border-radius:50%}
#b{left:6px;top:62px}#a{left:80px;top:30px}
#mid{display:flex;flex-direction:column;align-items:center;justify-content:space-between;height:150px;padding:6px 0}
#mid .k{position:static;width:84px;height:34px;font-size:12px;border-radius:999px}
.sh{display:flex;justify-content:space-between;grid-column:1/4;gap:10px}
.sh .k{position:static;flex:1;height:40px}
@media (orientation:landscape) and (max-height:520px){#pad{position:fixed;left:0;right:0;bottom:0;max-width:none;padding:0 18px 10px;pointer-events:none}#pad .k{pointer-events:auto;opacity:.82}.sh{position:fixed;top:44px;left:18px;right:18px}}
:root{--tile:#0d1424}
#grid{flex:1;min-height:0;display:grid;gap:10px;padding:6px 10px;align-items:center;justify-items:center}
.tile{position:relative;display:flex;flex-direction:column;align-items:center;gap:6px;width:100%;height:100%;min-height:0;justify-content:center;border-radius:10px;border:2px solid transparent;padding:4px}
.tile.focus{border-color:#4f8fff88}
.tile canvas{flex:none}
.tile .cap{display:flex;gap:8px;align-items:center;color:var(--dim);font-size:12px;letter-spacing:.04em}
.tile .cap select{background:#ffffff0b;color:var(--fg);border:1px solid var(--keyline);border-radius:999px;padding:2px 8px;font:inherit}
.tile .wait{position:absolute;inset:0;display:grid;place-items:center;color:var(--dim);font:500 12px system-ui;letter-spacing:.12em;pointer-events:none}
#panel .gbas label{display:inline-flex;margin:4px 12px 4px 0}
.tpad{display:grid;grid-template-columns:auto auto auto;gap:10px;align-items:center;justify-content:center;width:100%}
.tpad .k{position:static;width:36px;height:36px;border-radius:10px;font-size:12px}
.tpad .dp{display:grid;grid-template-columns:repeat(3,36px);grid-template-rows:repeat(3,36px);gap:2px}
.tpad .mid,.tpad .face{display:flex;gap:6px;align-items:center}
.tpad .mid .k{width:auto;padding:0 8px;border-radius:999px}
.tpad .face .k{border-radius:50%;width:42px;height:42px}
.tpad .sh{display:flex;gap:6px;grid-column:1/4;justify-content:center}
.tpad .sh .k{width:70px}
body.nopad .tpad{display:none}
#panel select{background:#ffffff0b;color:var(--fg);border:1px solid var(--keyline);border-radius:999px;padding:2px 8px;font:inherit}
</style></head><body>
<div id="app">
 <div id="bar">
  <span id="status">Connecting&hellip;</span>
  <select id="fps" title="Pictures a second"><option value="60">60 fps</option><option value="30">30 fps</option><option value="20">20 fps</option><option value="15">15 fps</option></select>
  <button id="look" title="Sharp or smooth pixels">Sharp</button>
  <button id="padbtn" title="Show or hide the on-screen buttons">Buttons</button>
  <button id="full" title="Full screen">&#9974;</button>
  <button id="gear" title="Settings">&#9881;</button>
  <button id="single" title="One GBA">One GBA</button>
 </div>
 <div id="panel" hidden>
  <h3>GBAS SHOWN</h3>
  <div class="gbas" id="gbas"></div>
  <h3>PAGE</h3>
  <label><input type="checkbox" id="showstatus"> Status line</label>
  <h3>BUTTONS</h3>
  <label>Keyboard keys for <select id="editgba"></select></label>
  <table id="binds"></table>
  <div class="row"><button id="defaults">Default Buttons</button></div>
  <h3>GBA</h3>
  <div class="row" id="gbaacts"></div>
 </div>
 <div id="grid"></div>
</div>
<script>
(function(){
'use strict';
function store(k,v){try{if(v===undefined)return localStorage.getItem(k);localStorage.setItem(k,v)}catch(e){return null}}
var statusEl=document.getElementById('status'),grid=document.getElementById('grid');
function say(t){statusEl.textContent=t}
function wsURL(p,port){return(location.protocol==='https:'?'wss://':'ws://')+location.host+p+'?p='+port}
document.getElementById('single').onclick=function(){location.href='/'};

/* ---- the buttons: the single page's, kept in this browser (A,B,Select,Start,Right,Left,Up,Down,R,L = 0..9; 10 Reset GBA) ---- */
var NAMES=['A','B','Select','Start','Right','Left','Up','Down','R','L','Reset GBA'];
var DEF_KEYS=['KeyX','KeyZ','Backspace','Enter','ArrowRight','ArrowLeft','ArrowUp','ArrowDown','KeyS','KeyA',''];
var DEF_PAD=[1,0,8,9,15,14,12,13,5,4,-1];
function loadList(k,d){try{var v=JSON.parse(store(k)||'null');if(Array.isArray(v)&&v.length===d.length)return v}catch(e){}return d.slice()}
var keys=loadList('gba.keys',DEF_KEYS),padb=loadList('gba.padbtn',DEF_PAD),capture=-1,captureKind='';

/* ---- each GBA shown: its tile, its two sockets, its own keyboard keys and on-screen buttons, and the
   gamepad that plays it ('pad0'..'pad3', or 'none') ---- */
var views={},padMask=[0,0,0,0],padReset=[false,false,false,false],padPrev={};
var kbdFor={},touchFor={},keysFor={},editPort=0;
var fps=store('gba.fps')||'60',fpsSel=document.getElementById('fps');fpsSel.value=fps;
function chosen(){try{var v=JSON.parse(store('gba.multi')||'null');if(Array.isArray(v))return v}catch(e){}return null}
var ports=[],order=[],firstPort=1;
/* The first GBA: the keyboard's usual keys and no gamepad; the next ones: no keys, gamepads 1, 2, 3. */
function who(port){var w=store('gba.multi.who'+port);if(w)return w;var i=order.indexOf(port);return i<=0?'none':'pad'+Math.min(3,i-1)}
var EMPTY=['','','','','','','','','','',''];
function keysOf(port){
 if(!keysFor[port])keysFor[port]=loadList('gba.keys.p'+port,port===firstPort?keys:EMPTY);
 return keysFor[port];
}
function mask(v){
 var m=kbdFor[v.port]||0,w=who(v.port),t=touchFor[v.port]||{};
 if(w.indexOf('pad')===0)m|=padMask[+w.slice(3)];
 for(var id in t)m|=t[id];
 return m;
}
function send(v){
 if(!v.input||v.input.readyState!==1)return;
 if(v.input.bufferedAmount>1024){v.input.close();return}
 v.input.send('k '+mask(v));
}
function changed(){paint();for(var p in views)send(views[p])}
function paint(){for(var p in views){var v=views[p],m=mask(v);v.tile.querySelectorAll('[data-k]').forEach(function(el){el.classList.toggle('on',!!(m&(1<<el.dataset.k)))})}}
setInterval(function(){for(var p in views)if(mask(views[p]))send(views[p])},250);
function resetGBA(port){var v=views[port];if(v&&v.input&&v.input.readyState===1)v.input.send('reset')}
/* A tile's own on-screen buttons. */
function tilePad(port){
 var d=document.createElement('div');d.className='tpad';
 d.innerHTML='<div class="sh"><div class="k" data-k="9">L</div><div class="k" data-k="8">R</div></div>'+
  '<div class="dp"><span></span><div class="k" data-k="6">&#9650;</div><span></span><div class="k" data-k="5">&#9664;</div><span></span><div class="k" data-k="4">&#9654;</div><span></span><div class="k" data-k="7">&#9660;</div><span></span></div>'+
  '<div class="mid"><div class="k" data-k="2">SEL</div><div class="k" data-k="3">START</div></div>'+
  '<div class="face"><div class="k" data-k="1">B</div><div class="k" data-k="0">A</div></div>';
 d.querySelectorAll('[data-k]').forEach(function(el){
  var bit=1<<el.dataset.k;
  el.addEventListener('pointerdown',function(e){e.preventDefault();e.stopPropagation();el.setPointerCapture(e.pointerId);var t=touchFor[port]=touchFor[port]||{};t[e.pointerId]=(t[e.pointerId]||0)|bit;changed()});
  var up=function(e){var t=touchFor[port]||{};if(t[e.pointerId]===undefined)return;t[e.pointerId]&=~bit;if(!t[e.pointerId])delete t[e.pointerId];changed()};
  el.addEventListener('pointerup',up);el.addEventListener('pointercancel',up);el.addEventListener('lostpointercapture',up);
 });
 return d;
}

function unpack(v,buf){
 if(buf.length<12||buf[0]!==71||buf[1]!==66||buf[2]!==65||buf[3]!==50)return false;
 var w=buf[4]|(buf[5]<<8),h=buf[6]|(buf[7]<<8),n=w*h,len=buf[8]+buf[9]*256+buf[10]*65536+buf[11]*16777216;
 if(n<=0||n>240*160||buf.length!==12+len)return false;
 if(!v.px||v.px.length!==n)v.px=new Uint16Array(n);
 var px=v.px,i=12,end=12+len,o=0,k,val,c;
 while(i<end&&o<n){
  c=buf[i++];
  if(c<128){k=c+1;if(o+k>n||i+2*k>end)return false;while(k--){px[o++]=buf[i]|(buf[i+1]<<8);i+=2}}
  else{k=(c&127)+3;if(o+k>n||i+2>end)return false;val=buf[i]|(buf[i+1]<<8);i+=2;while(k--)px[o++]=val}
 }
 if(o!==n)return false;
 for(i=w;i<n;i++)px[i]^=px[i-w];
 if(v.cv.width!==w||v.cv.height!==h){v.cv.width=w;v.cv.height=h;v.img=null;fit()}
 if(!v.img)v.img=v.ctx.createImageData(w,h);
 var d=v.img.data;
 for(i=0,o=0;i<n;i++,o+=4){val=px[i];var r=val&31,g=(val>>5)&31,b=(val>>10)&31;d[o]=(r<<3)|(r>>2);d[o+1]=(g<<3)|(g>>2);d[o+2]=(b<<3)|(b>>2);d[o+3]=255}
 v.ctx.putImageData(v.img,0,0);
 return true;
}
function connect(v){
 if(document.hidden)return;
 if(!v.video){
  var s=new WebSocket(wsURL('/video',v.port));v.video=s;s.binaryType='arraybuffer';
  s.onopen=function(){if(v.video===s)s.send('fps '+fps)};
  s.onmessage=function(ev){if(v.video!==s)return;s.send('ack');var b=new Uint8Array(ev.data);v.bytes+=b.length;if(unpack(v,b)){v.frames++;v.wait.hidden=true}};
  s.onclose=function(){if(v.video!==s)return;v.video=null;v.wait.hidden=false;if(views[v.port]===v&&!document.hidden)setTimeout(function(){connect(v)},1000)};
  s.onerror=function(){s.close()};
 }
 if(!v.input){
  var t=new WebSocket(wsURL('/input',v.port));v.input=t;
  t.onopen=function(){if(v.input===t)send(v)};
  t.onclose=function(){if(v.input!==t)return;v.input=null;if(views[v.port]===v&&!document.hidden)setTimeout(function(){connect(v)},1000)};
  t.onerror=function(){t.close()};
 }
}
function drop(v){var a=v.video,b=v.input;v.video=v.input=null;if(a)a.close();if(b)b.close()}

/* ---- the grid: the GBAs chosen (all by default), as big as they fit ---- */
function build(){
 var want=chosen()||ports;want=want.filter(function(p){return ports.indexOf(p)>=0});
 for(var p in views)if(want.indexOf(+p)<0){drop(views[p]);delete views[p]}
 grid.innerHTML='';
 want.forEach(function(port){
  var v=views[port];
  if(!v){
   v={port:port,video:null,input:null,frames:0,bytes:0,img:null,px:null};
   v.tile=document.createElement('div');v.tile.className='tile';
   v.cv=document.createElement('canvas');v.cv.width=240;v.cv.height=160;v.ctx=v.cv.getContext('2d');
   v.cap=document.createElement('div');v.cap.className='cap';
   v.wait=document.createElement('div');v.wait.className='wait';v.wait.textContent='WAITING FOR GBA '+port;
   v.tile.appendChild(v.cv);v.tile.appendChild(v.cap);v.tile.appendChild(tilePad(port));v.tile.appendChild(v.wait);
   views[port]=v;
  }
  var sel='<span>GBA '+port+'</span><select title="The gamepad that plays this GBA (its keyboard keys: Settings)">';
  [['none','No Gamepad'],['pad0','Gamepad 1'],['pad1','Gamepad 2'],['pad2','Gamepad 3'],['pad3','Gamepad 4']].forEach(function(o){sel+='<option value="'+o[0]+'"'+(who(port)===o[0]?' selected':'')+'>'+o[1]+'</option>'});
  v.cap.innerHTML=sel+'</select>';
  v.cap.querySelector('select').onchange=function(e){store('gba.multi.who'+port,e.target.value);changed()};
  v.cap.querySelector('select').addEventListener('pointerdown',function(e){e.stopPropagation()});
  grid.appendChild(v.tile);connect(v);
 });
 var n=want.length,cols=n<=1?1:n===2?(innerWidth>=innerHeight?2:1):2;
 grid.style.gridTemplateColumns='repeat('+cols+',1fr)';
 grid.style.gridTemplateRows='repeat('+Math.ceil(Math.max(n,1)/cols)+',1fr)';
 if(!n)grid.innerHTML='<div class="wait" style="position:static">NO GBA CHOSEN (SETTINGS)</div>';
 renderPanel();fit();paint();
}
function fit(){
 var dpr=window.devicePixelRatio||1;
 for(var p in views){
  var v=views[p],tp=v.tile.querySelector('.tpad'),ph=tp&&!document.body.classList.contains('nopad')?tp.offsetHeight+8:0;
  var aw=v.tile.clientWidth-12,ah=v.tile.clientHeight-36-ph,w=v.cv.width,h=v.cv.height;
  if(aw<=0||ah<=0)continue;
  var k=Math.floor(Math.min(aw*dpr/w,ah*dpr/h)),cw,ch;
  if(k>=1){cw=w*k/dpr;ch=h*k/dpr}else{var f=Math.min(aw/w,ah/h);cw=w*f;ch=h*f}
  v.cv.style.width=cw+'px';v.cv.style.height=ch+'px';
 }
}
addEventListener('resize',function(){build()});
function loadPorts(){
 fetch('/ports',{cache:'no-store'}).then(function(r){return r.json()}).then(function(list){
  if(!Array.isArray(list))return;
  if(JSON.stringify(list)!==JSON.stringify(ports)){ports=list;order=list.slice();firstPort=list.length?list[0]:1;build()}
 }).catch(function(){});
}
setInterval(loadPorts,5000);
setInterval(function(){var parts=[];for(var p in views){parts.push('GBA '+p+' '+views[p].frames+' fps');views[p].frames=0}say(parts.length?'Live  '+parts.join('  \u00b7  '):'Waiting')},1000);

/* ---- keyboard, gamepads, the on-screen buttons ---- */
addEventListener('keydown',function(e){
 if(capture>=0&&captureKind==='key'){
  e.preventDefault();
  if(e.code!=='Escape'){
   /* One button of this GBA (taken from its other button, if it had it); other GBAs may share it. */
   var mine=keysOf(editPort);
   for(var i=0;i<mine.length;i++)if(mine[i]===e.code)mine[i]='';
   mine[capture]=e.code;store('gba.keys.p'+editPort,JSON.stringify(mine));
  }
  capture=-1;renderBinds();return;
 }
 var hit=false;
 for(var p in views){
  var b=keysOf(+p).indexOf(e.code);if(b<0||!e.code)continue;hit=true;
  if(b===10){if(!e.repeat)resetGBA(+p);continue}
  kbdFor[p]=(kbdFor[p]||0)|(1<<b);
 }
 if(hit){e.preventDefault();changed()}
});
addEventListener('keyup',function(e){
 var hit=false;
 for(var p in views){var b=keysOf(+p).indexOf(e.code);if(b<0||b===10||!e.code)continue;hit=true;kbdFor[p]=(kbdFor[p]||0)&~(1<<b)}
 if(hit){e.preventDefault();changed()}
});
addEventListener('blur',function(){kbdFor={};touchFor={};changed()});
function pollPads(){
 var gps=navigator.getGamepads?navigator.getGamepads():[],any=false;
 for(var i=0;i<4;i++){
  var g=gps[i],m=0,rst=false;
  if(g&&g.connected){var b=g.buttons;
   if(capture>=0&&captureKind==='pad'){
    for(var n=0;n<b.length;n++)if(b[n].pressed&&!(padPrev[i]&&padPrev[i][n])){for(var j=0;j<padb.length;j++)if(padb[j]===n)padb[j]=-1;padb[capture]=n;store('gba.padbtn',JSON.stringify(padb));capture=-1;renderBinds();break}
   }else{
    for(var k=0;k<padb.length;k++){var n2=padb[k];if(n2>=0&&b[n2]&&b[n2].pressed){if(k===10)rst=true;else m|=1<<k}}
    var x=g.axes[0]||0,y=g.axes[1]||0;if(x>.5)m|=16;if(x<-.5)m|=32;if(y<-.5)m|=64;if(y>.5)m|=128;
   }
   padPrev[i]=b.map(function(v){return v.pressed});
  }
  if(rst&&!padReset[i])for(var p in views)if(who(+p)==='pad'+i)resetGBA(+p);
  padReset[i]=rst;
  if(m!==padMask[i]){padMask[i]=m;any=true}
 }
 if(any)changed();
 requestAnimationFrame(pollPads);
}
requestAnimationFrame(pollPads);

/* ---- the bar and the settings panel ---- */
var panel=document.getElementById('panel'),binds=document.getElementById('binds');
fpsSel.onchange=function(){fps=fpsSel.value;store('gba.fps',fps);for(var p in views){var s=views[p].video;if(s&&s.readyState===1)s.send('fps '+fps)}};
var smooth=store('gba.smooth')==='1',lookBtn=document.getElementById('look');
function applyLook(){document.body.classList.toggle('smooth',smooth);lookBtn.textContent=smooth?'Smooth':'Sharp'}
lookBtn.onclick=function(){smooth=!smooth;store('gba.smooth',smooth?'1':'0');applyLook()};
var coarse=!!(window.matchMedia&&matchMedia('(pointer:coarse)').matches),padOn=store('gba.pad');padOn=padOn===null?coarse:padOn==='1';
function applyPad(){document.body.classList.toggle('nopad',!padOn);fit()}
document.getElementById('padbtn').onclick=function(){padOn=!padOn;store('gba.pad',padOn?'1':'0');applyPad()};
document.getElementById('full').onclick=function(){try{if(document.fullscreenElement)document.exitFullscreen();else document.documentElement.requestFullscreen()}catch(e){}};
var showStatus=store('gba.status')!=='0',statusBox=document.getElementById('showstatus');
function applyStatus(){document.body.classList.toggle('nostatus',!showStatus);statusBox.checked=showStatus}
statusBox.onchange=function(){showStatus=statusBox.checked;store('gba.status',showStatus?'1':'0');applyStatus()};
document.getElementById('gear').onclick=function(){panel.hidden=!panel.hidden;capture=-1;renderPanel()};
function renderPanel(){
 var want=chosen()||ports,h='';
 ports.forEach(function(p){h+='<label><input type="checkbox" data-p="'+p+'"'+(want.indexOf(p)>=0?' checked':'')+'> GBA '+p+'</label>'});
 var g=document.getElementById('gbas');g.innerHTML=h||'No GBAs';
 g.querySelectorAll('input').forEach(function(c){c.onchange=function(){var list=[];g.querySelectorAll('input').forEach(function(x){if(x.checked)list.push(+x.dataset.p)});store('gba.multi',JSON.stringify(list));build()}});
 var a='';Object.keys(views).forEach(function(p){a+='<button class="danger" data-r="'+p+'">Reset GBA '+p+'</button><button data-l="'+p+'">Link GBA '+p+'</button>'});
 var acts=document.getElementById('gbaacts');acts.innerHTML=a;
 acts.querySelectorAll('[data-r]').forEach(function(b){var armed=0;b.onclick=function(){if(Date.now()-armed<3000){armed=0;resetGBA(+b.dataset.r);b.textContent='Reset GBA '+b.dataset.r;return}armed=Date.now();b.textContent='Press again'}});
 acts.querySelectorAll('[data-l]').forEach(function(b){b.onclick=function(){var v=views[b.dataset.l];if(v&&v.input&&v.input.readyState===1)v.input.send('disconnect')}});
 var es=document.getElementById('editgba'),eh='';
 Object.keys(views).forEach(function(p){eh+='<option value="'+p+'">GBA '+p+'</option>'});
 es.innerHTML=eh||'<option value="0">\u2014</option>';
 if(!views[editPort])editPort=+(Object.keys(views)[0]||0);
 es.value=String(editPort);
 es.onchange=function(){editPort=+es.value;capture=-1;renderBinds()};
 renderBinds();
}
function keyName(c){if(!c)return'\u2014';return c.replace(/^Key/,'').replace(/^Digit/,'').replace(/^Arrow/,'\u2192 ')}
function renderBinds(){
 var h='<tr><td></td><td>GBA '+editPort+' keys</td><td>Every gamepad</td></tr>';
 for(var i=0;i<NAMES.length;i++){
  var kw=capture===i&&captureKind==='key',pw=capture===i&&captureKind==='pad';
  h+='<tr><td>'+NAMES[i]+'</td><td><button data-i="'+i+'" data-t="key" class="'+(kw?'wait':'')+'">'+(kw?'Press a key':keyName(keysOf(editPort)[i]))+'</button></td>'+
     '<td><button data-i="'+i+'" data-t="pad" class="'+(pw?'wait':'')+'">'+(pw?'Press a button':(padb[i]>=0?'Button '+padb[i]:'\u2014'))+'</button></td></tr>';
 }
 binds.innerHTML=h;
 binds.querySelectorAll('button').forEach(function(b){
  b.onclick=function(){var i=+b.dataset.i,t=b.dataset.t;if(capture===i&&captureKind===t)capture=-1;else{capture=i;captureKind=t}renderBinds()};
  b.oncontextmenu=function(e){e.preventDefault();var i=+b.dataset.i;if(b.dataset.t==='key'){var ks=keysOf(editPort);ks[i]='';store('gba.keys.p'+editPort,JSON.stringify(ks))}else{padb[i]=-1;store('gba.padbtn',JSON.stringify(padb))}capture=-1;renderBinds()};
 });
}
document.getElementById('defaults').onclick=function(){
 /* The usual keys on the GBA being edited (the others keep theirs, shared or not), the gamepads' usual buttons. */
 keysFor[editPort]=DEF_KEYS.slice();store('gba.keys.p'+editPort,JSON.stringify(keysFor[editPort]));
 padb=DEF_PAD.slice();store('gba.padbtn',JSON.stringify(padb));capture=-1;renderBinds()};
document.addEventListener('visibilitychange',function(){for(var p in views){if(document.hidden)drop(views[p]);else connect(views[p])}});
applyLook();applyPad();applyStatus();loadPorts();
})();
</script></body></html>)HTML";
