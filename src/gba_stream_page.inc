/* Porpoise - the GBA's page (gba_stream.cpp), in PS5-ECAM's remote touch screen's look (its
 * port/stream/browser.h: the dark field, the waiting splash). Included inside a namespace.
 * SPDX-License-Identifier: GPL-3.0-or-later */
constexpr const char *kPage = R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no,viewport-fit=cover">
<title>GBA - Porpoise</title>
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
</style></head><body>
<div id="app">
 <div id="bar">
  <span id="status">Connecting&hellip;</span>
  <select id="port" title="Which GBA" hidden></select>
  <select id="fps" title="Pictures a second"><option value="60">60 fps</option><option value="30">30 fps</option><option value="20">20 fps</option><option value="15">15 fps</option></select>
  <button id="look" title="Sharp or smooth pixels">Sharp</button>
  <button id="padbtn" title="Show or hide the on-screen buttons">Buttons</button>
  <button id="full" title="Full screen">&#9974;</button>
  <button id="gear" title="Settings">&#9881;</button>
  <button id="multi" title="Several GBAs at once">All GBAs</button>
 </div>
 <div id="panel" hidden>
  <h3>PAGE</h3>
  <label><input type="checkbox" id="showstatus"> Status line</label>
  <h3>BUTTONS</h3>
  <table id="binds"></table>
  <div class="row"><button id="defaults">Default Buttons</button></div>
  <h3>GBA</h3>
  <div class="row"><button id="reset" class="danger">Reset GBA</button><button id="link">Disconnect / Reconnect</button></div>
 </div>
 <div id="screen"><canvas id="cv" width="240" height="160"></canvas>
  <div id="splash" role="status" aria-live="polite"><div class="splash"><div class="mark">GBA</div>
   <p class="msg" id="msg">Waiting for the GBA</p><div class="dots" aria-hidden="true"><i></i><i></i><i></i></div></div></div>
 </div>
 <div id="pad">
  <div class="sh"><div class="k" data-k="9">L</div><div class="k" data-k="8">R</div></div>
  <div class="grp" id="dpad"><div class="k" id="up" data-k="6">&#9650;</div><div class="k" id="down" data-k="7">&#9660;</div><div class="k" id="left" data-k="5">&#9664;</div><div class="k" id="right" data-k="4">&#9654;</div></div>
  <div id="mid"><div class="k" data-k="2">SELECT</div><div class="k" data-k="3">START</div></div>
  <div class="grp" id="face"><div class="k" id="b" data-k="1">B</div><div class="k" id="a" data-k="0">A</div></div>
 </div>
</div>
<script>
(function(){
'use strict';
var statusEl=document.getElementById('status'),msgEl=document.getElementById('msg'),splash=document.getElementById('splash');
var cv=document.getElementById('cv'),ctx=cv.getContext('2d'),box=document.getElementById('screen');
function store(k,v){try{if(v===undefined)return localStorage.getItem(k);localStorage.setItem(k,v)}catch(e){return null}}
function say(t){statusEl.textContent=t}
/* Which GameCube port's GBA: ?p=N, or the last chosen here, else the lowest with one. */
var portSel=document.getElementById('port'),port=+(new URLSearchParams(location.search).get('p')||store('gba.port')||0);
var wsURL=function(p){return(location.protocol==='https:'?'wss://':'ws://')+location.host+p+(port?'?p='+port:'')};
function loadPorts(){
 fetch('/ports',{cache:'no-store'}).then(function(r){return r.json()}).then(function(list){
  if(!Array.isArray(list))return;
  var h='';list.forEach(function(n){h+='<option value="'+n+'">GBA '+n+'</option>'});
  if(portSel.innerHTML!==h)portSel.innerHTML=h;
  portSel.hidden=list.length<2;
  if(list.length&&list.indexOf(port)<0){port=list[0];reconnect()}
  portSel.value=String(port);
 }).catch(function(){});
}
portSel.onchange=function(){port=+portSel.value;store('gba.port',String(port));reconnect()};
function reconnect(){var i=input,v=video;input=video=null;if(i)i.close();if(v)v.close();kbd=0;touch={};connectInput();connectVideo()}
setInterval(loadPorts,5000);

/* ---- buttons: keyboard + touch + gamepad, as mGBA's key bits (A,B,Select,Start,Right,Left,Up,Down,R,L = 0..9) ---- */
var kbd=0,touch={},pad=0,input=null,sentMask=-1;
function mask(){var m=kbd|pad;for(var id in touch)m|=touch[id];return m}
/* What each GBA button is on: a keyboard key and a gamepad button (the standard mapping's numbers),
   changed in the settings panel and kept in this browser. Bit 10 is Reset GBA (not a GBA key). */
var NAMES=['A','B','Select','Start','Right','Left','Up','Down','R','L','Reset GBA'];
var DEF_KEYS=['KeyX','KeyZ','Backspace','Enter','ArrowRight','ArrowLeft','ArrowUp','ArrowDown','KeyS','KeyA',''];
var DEF_PAD=[1,0,8,9,15,14,12,13,5,4,-1];
function loadList(k,d){try{var v=JSON.parse(store(k)||'null');if(Array.isArray(v)&&v.length===d.length)return v}catch(e){}return d.slice()}
var keys=loadList('gba.keys',DEF_KEYS),padb=loadList('gba.padbtn',DEF_PAD),capture=-1,captureKind='';
function bitOfKey(code){for(var i=0;i<keys.length;i++)if(keys[i]===code)return i;return -1}
var resetLatch=false;
function resetGBA(){if(input&&input.readyState===1)input.send('reset')}
addEventListener('keydown',function(e){
 if(capture>=0&&captureKind==='key'){e.preventDefault();if(e.code!=='Escape'){for(var i=0;i<keys.length;i++)if(keys[i]===e.code)keys[i]='';keys[capture]=e.code;store('gba.keys',JSON.stringify(keys))}capture=-1;renderBinds();return}
 var b=bitOfKey(e.code);if(b<0)return;e.preventDefault();
 if(b===10){if(!e.repeat)resetGBA();return}
 kbd|=1<<b;changed()});
addEventListener('keyup',function(e){var b=bitOfKey(e.code);if(b<0||b===10)return;e.preventDefault();kbd&=~(1<<b);changed()});
addEventListener('blur',function(){kbd=0;touch={};changed()});
function paint(){var m=mask();document.querySelectorAll('[data-k]').forEach(function(el){el.classList.toggle('on',!!(m&(1<<el.dataset.k)))})}
document.querySelectorAll('[data-k]').forEach(function(el){
 var bit=1<<el.dataset.k;
 el.addEventListener('pointerdown',function(e){e.preventDefault();el.setPointerCapture(e.pointerId);touch[e.pointerId]=(touch[e.pointerId]||0)|bit;changed()});
 var up=function(e){if(touch[e.pointerId]===undefined)return;touch[e.pointerId]&=~bit;if(!touch[e.pointerId])delete touch[e.pointerId];changed()};
 el.addEventListener('pointerup',up);el.addEventListener('pointercancel',up);el.addEventListener('lostpointercapture',up);
});
var padPrev={};
function pollPad(){
 var gps=navigator.getGamepads?navigator.getGamepads():[],m=0,rst=false;
 for(var i=0;i<gps.length;i++){var g=gps[i];if(!g||!g.connected)continue;var b=g.buttons;
  if(capture>=0&&captureKind==='pad'){
   for(var n=0;n<b.length;n++)if(b[n].pressed&&!(padPrev[i]&&padPrev[i][n])){for(var j=0;j<padb.length;j++)if(padb[j]===n)padb[j]=-1;padb[capture]=n;store('gba.padbtn',JSON.stringify(padb));capture=-1;renderBinds();break}
  }else{
   for(var k=0;k<padb.length;k++){var n2=padb[k];if(n2>=0&&b[n2]&&b[n2].pressed){if(k===10)rst=true;else m|=1<<k}}
   var x=g.axes[0]||0,y=g.axes[1]||0;if(x>.5)m|=16;if(x<-.5)m|=32;if(y<-.5)m|=64;if(y>.5)m|=128;
  }
  padPrev[i]=b.map(function(v){return v.pressed});
 }
 if(rst&&!resetLatch)resetGBA();resetLatch=rst;
 if(m!==pad){pad=m;changed()}
 requestAnimationFrame(pollPad);
}
requestAnimationFrame(pollPad);
function send(){
 if(!input||input.readyState!==1)return;
 if(input.bufferedAmount>1024){input.close();return}  /* reconnect rather than queue old presses */
 var m=mask();input.send('k '+m);sentMask=m;
}
function changed(){paint();send()}
setInterval(function(){if(mask()!==0)send()},250); /* a held button stays held; the PS5 lets go of a quiet page */
function connectInput(){
 if(document.hidden||input)return;
 var s=new WebSocket(wsURL('/input'));input=s;
 s.onopen=function(){if(input===s)send()};
 s.onclose=function(){if(input!==s)return;input=null;if(!document.hidden)setTimeout(connectInput,1000)};
 s.onerror=function(){s.close()};
}

/* ---- the settings panel ---- */
var panel=document.getElementById('panel'),gear=document.getElementById('gear'),binds=document.getElementById('binds');
var showStatus=store('gba.status')!=='0',statusBox=document.getElementById('showstatus');
function applyStatus(){document.body.classList.toggle('nostatus',!showStatus);statusBox.checked=showStatus}
statusBox.onchange=function(){showStatus=statusBox.checked;store('gba.status',showStatus?'1':'0');applyStatus()};
gear.onclick=function(){panel.hidden=!panel.hidden;capture=-1;renderBinds()};
function keyName(c){if(!c)return'\u2014';return c.replace(/^Key/,'').replace(/^Digit/,'').replace(/^Arrow/,'\u2192 ').replace('Right','Right')}
function renderBinds(){
 var h='<tr><td></td><td>Keyboard</td><td>Gamepad</td></tr>';
 for(var i=0;i<NAMES.length;i++){
  var kw=capture===i&&captureKind==='key',pw=capture===i&&captureKind==='pad';
  h+='<tr><td>'+NAMES[i]+'</td><td><button data-i="'+i+'" data-t="key" class="'+(kw?'wait':'')+'">'+(kw?'Press a key':keyName(keys[i]))+'</button></td>'+
     '<td><button data-i="'+i+'" data-t="pad" class="'+(pw?'wait':'')+'">'+(pw?'Press a button':(padb[i]>=0?'Button '+padb[i]:'\u2014'))+'</button></td></tr>';
 }
 binds.innerHTML=h;
 binds.querySelectorAll('button').forEach(function(b){
  b.onclick=function(){var i=+b.dataset.i,t=b.dataset.t;if(capture===i&&captureKind===t){capture=-1}else{capture=i;captureKind=t}renderBinds()};
  b.oncontextmenu=function(e){e.preventDefault();var i=+b.dataset.i;if(b.dataset.t==='key'){keys[i]='';store('gba.keys',JSON.stringify(keys))}else{padb[i]=-1;store('gba.padbtn',JSON.stringify(padb))}capture=-1;renderBinds()};
 });
}
document.getElementById('defaults').onclick=function(){keys=DEF_KEYS.slice();padb=DEF_PAD.slice();store('gba.keys',JSON.stringify(keys));store('gba.padbtn',JSON.stringify(padb));capture=-1;renderBinds()};
var resetArmed=0;
document.getElementById('reset').onclick=function(e){
 var b=e.target;if(Date.now()-resetArmed<3000){resetArmed=0;resetGBA();b.textContent='Reset GBA';return}
 resetArmed=Date.now();b.textContent='Press again to reset';setTimeout(function(){if(resetArmed&&Date.now()-resetArmed>=3000){resetArmed=0;b.textContent='Reset GBA'}},3100);
};
document.getElementById('link').onclick=function(){if(input&&input.readyState===1)input.send('disconnect')};
document.getElementById('multi').onclick=function(){location.href='/multi'};
applyStatus();renderBinds();

/* ---- layout: the biggest sharp size that fits ---- */
var padBtn=document.getElementById('padbtn'),fullBtn=document.getElementById('full'),lookBtn=document.getElementById('look');
var coarse=!!(window.matchMedia&&matchMedia('(pointer:coarse)').matches);
var padOn=store('gba.pad');padOn=padOn===null?coarse:padOn==='1';
var smooth=store('gba.smooth')==='1';
function applyLook(){document.body.classList.toggle('smooth',smooth);lookBtn.textContent=smooth?'Smooth':'Sharp'}
lookBtn.onclick=function(){smooth=!smooth;store('gba.smooth',smooth?'1':'0');applyLook()};
function applyPad(){document.body.classList.toggle('nopad',!padOn);fit()}
padBtn.onclick=function(){padOn=!padOn;store('gba.pad',padOn?'1':'0');applyPad()};
fullBtn.onclick=function(){try{if(document.fullscreenElement)document.exitFullscreen();else document.documentElement.requestFullscreen()}catch(e){}};
function fit(){
 var aw=box.clientWidth,ah=box.clientHeight,w=cv.width,h=cv.height;
 if(aw<=0||ah<=0)return;
 var dpr=window.devicePixelRatio||1,k=Math.floor(Math.min(aw*dpr/w,ah*dpr/h)),cw,ch;
 if(k>=1){cw=w*k/dpr;ch=h*k/dpr}else{var f=Math.min(aw/w,ah/h);cw=w*f;ch=h*f}
 cv.style.width=cw+'px';cv.style.height=ch+'px';
}
addEventListener('resize',fit);
if(window.ResizeObserver)new ResizeObserver(fit).observe(box);
applyLook();applyPad();

/* ---- pictures: pushed over /video, lossless ('GBA2': width, height, length, then each pixel XOR the
   one above as PackBits-style 16-bit runs and literals; 15-bit colour, red in the low bits). Each is
   answered with "ack" as it arrives, so the PS5 knows how far ahead it is. ---- */
var fpsSel=document.getElementById('fps'),fps=store('gba.fps')||'60';fpsSel.value=fps;
var video=null,frames=0,bytes=0,t0=performance.now(),stale=null,img=null,px=null;
function waiting(t){clearTimeout(stale);stale=null;splash.hidden=false;msgEl.textContent=t}
fpsSel.onchange=function(){fps=fpsSel.value;store('gba.fps',fps);if(video&&video.readyState===1)video.send('fps '+fps)};
function shown(){
 splash.hidden=true;clearTimeout(stale);
 stale=setTimeout(function(){say('Live  (screen unchanged)  '+cv.width+'x'+cv.height)},1500);
}
function draw(buf){
 if(buf.length<12||buf[0]!==71||buf[1]!==66||buf[2]!==65||buf[3]!==50)return false;
 var w=buf[4]|(buf[5]<<8),h=buf[6]|(buf[7]<<8),n=w*h,len=buf[8]+buf[9]*256+buf[10]*65536+buf[11]*16777216;
 if(n<=0||n>240*160||buf.length!==12+len)return false;
 if(!px||px.length!==n)px=new Uint16Array(n);
 var i=12,end=12+len,o=0,k,v,c;
 while(i<end&&o<n){
  c=buf[i++];
  if(c<128){k=c+1;if(o+k>n||i+2*k>end)return false;while(k--){px[o++]=buf[i]|(buf[i+1]<<8);i+=2}}
  else{k=(c&127)+3;if(o+k>n||i+2>end)return false;v=buf[i]|(buf[i+1]<<8);i+=2;while(k--)px[o++]=v}
 }
 if(o!==n)return false;
 for(i=w;i<n;i++)px[i]^=px[i-w];
 if(cv.width!==w||cv.height!==h){cv.width=w;cv.height=h;img=null;fit()}
 if(!img)img=ctx.createImageData(w,h);
 var d=img.data;
 for(i=0,o=0;i<n;i++,o+=4){v=px[i];var r=v&31,g=(v>>5)&31,b=(v>>10)&31;d[o]=(r<<3)|(r>>2);d[o+1]=(g<<3)|(g>>2);d[o+2]=(b<<3)|(b>>2);d[o+3]=255}
 ctx.putImageData(img,0,0);
 return true;
}
function connectVideo(){
 if(document.hidden||video)return;
 var s=new WebSocket(wsURL('/video'));video=s;s.binaryType='arraybuffer';
 s.onopen=function(){if(video!==s)return;s.send('fps '+fps);say('Connected');waiting('Waiting for the GBA')};
 s.onmessage=function(ev){
  if(video!==s)return;
  s.send('ack');
  var buf=new Uint8Array(ev.data);bytes+=buf.length;
  if(!draw(buf))return;
  frames++;shown();
  var t=performance.now();
  if(t-t0>1000){var dt=(t-t0)/1000;say('Live  '+Math.round(frames/dt)+' fps  '+cv.width+'x'+cv.height+'  ·  '+(frames?Math.round(bytes/frames/1024*10)/10:0)+' KB/frame');frames=0;bytes=0;t0=t}
 };
 s.onclose=function(){if(video!==s)return;video=null;say('Disconnected. Retrying…');waiting('Reconnecting to the PS5');if(!document.hidden)setTimeout(connectVideo,1000)};
 s.onerror=function(){s.close()};
}
function disconnect(){var i=input,v=video;input=video=null;if(i)i.close();if(v)v.close();waiting('Paused')}
addEventListener('pagehide',disconnect);
document.addEventListener('visibilitychange',function(){if(document.hidden)disconnect();else{connectInput();connectVideo()}});
loadPorts();connectInput();connectVideo();
})();
</script></body></html>)HTML";
