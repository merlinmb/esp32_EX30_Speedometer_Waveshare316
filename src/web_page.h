#pragma once
// Single-page settings app served by the speedometer (setup hotspot or car Wi-Fi).
static const char WEB_PAGE[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>EX30 Speedometer</title>
<style>
:root{--bg:#0b0e11;--panel:#141a20;--line:#25303a;--txt:#e8ecef;--sub:#9aa5af;--acc:#2ee6c8}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--txt);font:15px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
main{max-width:720px;margin:0 auto;padding:16px}
h1{font-size:22px;margin:4px 0 12px}h2{font-size:17px;margin:0 0 12px}
section{background:var(--panel);border-radius:14px;padding:16px;margin:0 0 14px}
label{display:flex;justify-content:space-between;align-items:center;gap:12px;padding:8px 0;border-bottom:1px solid var(--line)}
label:last-child{border-bottom:0}
input[type=text],input[type=password],input[type=number],input[type=url],select{background:#0b0e11;color:var(--txt);border:1px solid var(--line);border-radius:8px;padding:8px;min-width:0;width:60%}
input[type=color]{width:56px;height:34px;border:0;background:none;padding:0}
button{background:var(--acc);color:#001a16;border:0;border-radius:10px;padding:10px 16px;font-weight:600;font-size:15px;cursor:pointer}
button.sec{background:#25303a;color:var(--txt)}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}
.net{display:flex;justify-content:space-between;align-items:center;padding:10px 0;border-bottom:1px solid var(--line)}
.sub{color:var(--sub);font-size:13px}
.prev{display:flex;gap:16px;flex-wrap:wrap;justify-content:center;margin-bottom:8px}
.tabs{display:flex;gap:8px;margin-bottom:10px}
.tabs button{flex:1}
#toast{position:fixed;left:50%;bottom:20px;transform:translateX(-50%);background:#25303a;padding:10px 16px;border-radius:10px;display:none}
</style></head><body><main>
<h1>EX30 Speedometer</h1>
<section><h2>Live</h2><div id="live" class="sub">Connecting...</div></section>

<section><h2>Wi-Fi</h2>
<div class="sub" id="wifistate"></div>
<h3 style="font-size:15px;margin:14px 0 4px">Network 1 - phone hotspot (travelling)</h3>
<label>Name<input type="text" id="n0" autocapitalize="off" autocorrect="off"></label>
<label>Password<input type="password" id="p0"></label>
<div class="row" style="margin:8px 0 4px"><button onclick="saveNet(0)">Save network 1</button><button class="sec" onclick="forget(0)">Clear</button></div>
<h3 style="font-size:15px;margin:18px 0 4px">Network 2 - home Wi-Fi (updates, settings at home)</h3>
<label>Name<input type="text" id="n1" autocapitalize="off" autocorrect="off"></label>
<label>Password<input type="password" id="p1"></label>
<div class="row" style="margin:8px 0 4px"><button onclick="saveNet(1)">Save network 2</button><button class="sec" onclick="forget(1)">Clear</button></div>
<label>Use home when both are in range<input type="checkbox" id="preferHome"></label>
<label>Wi-Fi on<input type="checkbox" id="wifiEnabled"></label>
<div class="row" style="margin:10px 0"><button class="sec" onclick="scan()">Scan for networks</button><span class="sub" id="scanstate"></span></div>
<div id="scan"></div>
<p class="sub">Leave a password blank to keep the saved one. The name must match exactly; scanning fills it in for you. On an iPhone, turn on Maximise Compatibility in Personal Hotspot so it uses 2.4 GHz.</p>
</section>

<section><h2>Firmware updates</h2>
<label>OTA password (6+ characters)<input type="password" id="otaPass" placeholder="(unchanged)"></label>
<p class="sub">Upload a new firmware.bin at <a href="/update" style="color:var(--acc)">/update</a> (user admin), or from PlatformIO with the <b>ota</b> environment while on the same network.</p>
</section>

<section><h2>Display</h2>
<label>Units<select id="units"><option value="0">mph</option><option value="1">km/h</option></select></label>
<label>Design<select id="design"><option value="0">A Clean</option><option value="1">B Gauge</option><option value="2">B Gauge solid</option><option value="3">C Info</option><option value="4">D Night</option></select></label>
<label>Switch to D automatically at night<input type="checkbox" id="autoNight"></label>
<label>Night below sun angle (deg)<input type="number" id="nightBelowDeg" step="0.5" min="-18" max="10"></label>
<label>Brightness<select id="brightMode"><option value="0">Auto by sun</option><option value="1">Fixed</option></select></label>
<label>Fixed level<select id="fixedLevel"><option value="0">100%</option><option value="1">75%</option><option value="2">50%</option><option value="3">30%</option><option value="4">15%</option></select></label>
<label>GPS updates<select id="gpsHz"><option value="1">1 Hz</option><option value="5">5 Hz</option><option value="10">10 Hz</option></select></label>
<label>Smoothing<select id="smoothing"><option value="0">Low</option><option value="1">Medium</option><option value="2">High</option></select></label>
<label>Speed alert<select id="alertMode"><option value="0">Off</option><option value="1">Visual</option><option value="2">Visual reminder</option></select></label>
<label>Alert limit (0 = off)<input type="number" id="alertLimit" min="0" max="200" step="1"></label>
<label>Dim after stopped for (min, 0 = never)<input type="number" id="idleDimMin" min="0" max="60"></label>
<label>Then screen off after (min, 0 = stay dim)<input type="number" id="idleOffMin" min="0" max="60"></label>
</section>

<section><h2>Colours</h2>
<div class="tabs"><button id="tday" onclick="tab('day')">Day (A, B, C)</button><button id="tnight" class="sec" onclick="tab('night')">Night (D)</button></div>
<div class="prev"><svg id="pv" width="220" height="220" viewBox="0 0 466 466" aria-label="Preview"></svg></div>
<div class="row" id="presets" style="margin-bottom:8px"></div>
<div id="colours"></div>
</section>

<section><h2>Remote API</h2>
<label>Send location and speed<input type="checkbox" id="telemetryEnabled"></label>
<label>API URL<input type="url" id="apiUrl" placeholder="https://example.com/api/points"></label>
<label>Bearer token<input type="password" id="apiToken" placeholder="(unchanged)"></label>
<label>Record every (s)<input type="number" id="sampleSec" min="1" max="600"></label>
<label>Send every (s)<input type="number" id="uploadSec" min="2" max="3600"></label>
<label>Device name<input type="text" id="deviceName"></label>
<label>Time zone (POSIX)<input type="text" id="tz"></label>
<div class="sub" id="tele"></div>
</section>

<div class="row"><button onclick="save()">Save settings</button><button class="sec" onclick="post('/api/trip/reset',{}).then(()=>toast('Trip reset'))">Reset trip</button></div>
<p class="sub">Tip: hold the BOOT key to open settings. Wi-Fi setup is in that menu.</p>
</main><div id="toast"></div>
<script>
const EL=[['background','Background'],['speed','Speed digits'],['unit','Units label'],['ringTrack','Ring, unlit'],['ringFill','Ring, lit'],['status','Top status text'],['statusIcon','Status icon'],['info','Bottom info line'],['alert','Over limit'],['tickMajor','Gauge major ticks (B)'],['tickMinor','Gauge minor ticks (B)'],['tickLabel','Gauge numbers (B)'],['limitMark','Limit marker (B)']];
let S={},cur='day',P={};
const $=id=>document.getElementById(id);
function toast(t){const e=$('toast');e.textContent=t;e.style.display='block';setTimeout(()=>e.style.display='none',2200)}
async function get(u){const r=await fetch(u);return r.json()}
async function post(u,b){const r=await fetch(u,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)});return r.json()}
const FIELDS=['units','design','nightBelowDeg','brightMode','fixedLevel','gpsHz','smoothing','alertMode','alertLimit','idleDimMin','idleOffMin','apiUrl','sampleSec','uploadSec','deviceName','tz'];
const CHECKS=['wifiEnabled','telemetryEnabled','preferHome','autoNight'];
async function load(){S=await get('/api/settings');P=await get('/api/presets');
FIELDS.forEach(f=>{if($(f))$(f).value=S[f]});CHECKS.forEach(f=>$(f).checked=!!S[f]);
renderSaved();tab(cur)}
function renderSaved(){(S.networks||[]).forEach((n,i)=>{if($('n'+i)){$('n'+i).value=n.ssid||'';$('p'+i).value='';$('p'+i).placeholder=n.passSet?'(saved)':''}})}
async function saveNet(i){const ssid=$('n'+i).value.trim();if(!ssid){toast('Enter a network name');return}
S=await post('/api/wifi',{slot:i,ssid:ssid,pass:$('p'+i).value});renderSaved();toast('Network '+(i+1)+' saved')}
async function forget(i){S=await post('/api/wifi/forget',{slot:i});renderSaved();toast('Network '+(i+1)+' cleared')}
function esc(s){return s.replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]))}
function tab(t){cur=t;$('tday').className=t=='day'?'':'sec';$('tnight').className=t=='night'?'':'sec';
const c=S[t];const box=$('colours');box.innerHTML='';
EL.forEach(([k,n])=>{const l=document.createElement('label');l.textContent=n;const i=document.createElement('input');i.type='color';i.value=c[k];i.oninput=()=>{c[k]=i.value;preview()};l.appendChild(i);box.appendChild(l)});
const pr=$('presets');pr.innerHTML='<span class="sub">Presets:</span>';(P[t]||[]).forEach(p=>{const b=document.createElement('button');b.className='sec';b.textContent=p.name;b.onclick=()=>{S[t]=Object.assign({},p.c);tab(t)};pr.appendChild(b)});
preview()}
function pol(r,d){const a=d*Math.PI/180;return[233+r*Math.sin(a),233-r*Math.cos(a)]}
function arc(r,a0,a1){const p0=pol(r,a0),p1=pol(r,a1);return`M${p0[0]} ${p0[1]}A${r} ${r} 0 ${a1-a0>180?1:0} 1 ${p1[0]} ${p1[1]}`}
function preview(){const c=S[cur];let s=`<circle cx="233" cy="233" r="233" fill="${c.background}"/>`;
if(cur=='day'){s+=`<path d="${arc(218,-135,135)}" stroke="${c.ringTrack}" stroke-width="8" fill="none" stroke-linecap="round"/><path d="${arc(218,-135,27)}" stroke="${c.ringFill}" stroke-width="8" fill="none" stroke-linecap="round"/>`+
`<circle cx="160" cy="70" r="6" fill="${c.statusIcon}"/><text x="240" y="77" text-anchor="middle" fill="${c.status}" font-size="18" font-family="sans-serif">14 SATS | 10 Hz</text>`+
`<text x="233" y="300" text-anchor="middle" fill="${c.speed}" font-size="200" font-weight="600" font-family="Barlow Condensed,Arial Narrow,sans-serif">72</text>`}
else{for(let i=0;i<46;i++){const t=i*120/45,p=pol(214,-135+270*t/120),lit=t<=48;s+=`<circle cx="${p[0]}" cy="${p[1]}" r="${lit?3.4:2.4}" fill="${lit?c.ringFill:c.ringTrack}"/>`}
s+=`<circle cx="170" cy="82" r="6" fill="${c.statusIcon}"/><text x="240" y="88" text-anchor="middle" fill="${c.status}" font-size="17" font-family="sans-serif">NIGHT 15%</text>`+
`<text x="233" y="295" text-anchor="middle" fill="${c.speed}" font-size="190" font-weight="600" font-family="Barlow Condensed,Arial Narrow,sans-serif">48</text>`}
s+=`<text x="233" y="345" text-anchor="middle" fill="${c.unit}" font-size="22" letter-spacing="6" font-family="sans-serif">${S.units==1?'KM/H':'MPH'}</text><text x="233" y="398" text-anchor="middle" fill="${c.info}" font-size="18" font-family="sans-serif">18:56 · 12.4 mi</text>`;
$('pv').innerHTML=s}
async function save(){FIELDS.forEach(f=>{const e=$(f);if(!e)return;S[f]=(e.type=='number'||e.tagName=='SELECT')?Number(e.value):e.value});
CHECKS.forEach(f=>S[f]=$(f).checked);const tok=$('apiToken').value;const b=Object.assign({},S);if(tok)b.apiToken=tok;
const op=$('otaPass').value;if(op){if(op.length<6){toast('OTA password needs 6+ characters');return}b.otaPass=op}delete b.networks;
S=await post('/api/settings',b);$('apiToken').value='';$('otaPass').value='';toast('Saved');tab(cur)}
async function scan(){$('scanstate').textContent='Scanning...';let r;for(let i=0;i<20;i++){r=await get('/api/scan');if(!r.scanning)break;await new Promise(z=>setTimeout(z,700))}
$('scanstate').textContent='';const d=$('scan');d.innerHTML='';(r.networks||[]).forEach(n=>{const e=document.createElement('div');e.className='net';
e.innerHTML='<span>'+esc(n.ssid)+' <span class="sub">'+n.rssi+' dBm'+(n.open?' open':'')+'</span></span>';const g=document.createElement('span');g.className='row';[0,1].forEach(i=>{const b=document.createElement('button');b.className='sec';b.textContent='Use as '+(i+1);
b.onclick=()=>{$('n'+i).value=n.ssid;$('p'+i).value='';$('p'+i).focus();toast('Enter the password, then save network '+(i+1))};g.appendChild(b)});e.appendChild(g);d.appendChild(e)})}
async function live(){try{const l=await get('/api/status');
$('live').textContent=(l.fix?l.speed+' '+(S.units==1?'km/h':'mph'):'No GPS fix')+' · '+l.sats+' sats · '+l.hz.toFixed(1)+' Hz · brightness '+l.bright+'%'+(l.night?' (night)':'')+(l.screen==2?' · screen off':l.screen==1?' · screen dimmed':'');
$('wifistate').textContent=l.wifi?('Connected to '+l.ssid+' ('+l.ip+', '+l.rssi+' dBm)'):'Not connected to Wi-Fi';
$('tele').textContent='Queued '+l.queued+', sent '+l.sent+', last HTTP '+l.http}catch(e){}}
load();live();setInterval(live,2000);
</script></body></html>)HTML";
