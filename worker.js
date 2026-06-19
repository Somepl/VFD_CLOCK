/**
 * Cloudflare Worker — 时钟远程控制（MQTT 版）
 *
 * 完整功能：状态监视、数字显示、亮度、夜间模式、6种内置动画、
 * 图案编辑（可视段编辑器）、动画编辑（帧序列组合）
 *
 * MQTT 架构：手机 → 控制页 → MQTT(WS) → 公共 Broker → MQTT → ESP32
 * 主题: clock/<密码>/cmd (发命令), clock/<密码>/status (收状态)
 */

const MQTT_BROKER = 'wss://broker.emqx.io:8084/mqtt';

export default {
  async fetch(request, env) {
    const PWD = env.PASSWORD || '';
    const url = new URL(request.url);

    if (url.pathname === '/api/auth' && request.method === 'POST') {
      const auth = request.headers.get('X-Password') || url.searchParams.get('pwd');
      if (auth === PWD) {
        return new Response(JSON.stringify({ success: true }), {
          headers: { 'Content-Type': 'application/json' }
        });
      }
      return new Response(JSON.stringify({ success: false }), {
        status: 401, headers: { 'Content-Type': 'application/json' }
      });
    }

    return new Response(CONTROL_HTML, {
      headers: { 'Content-Type': 'text/html; charset=utf-8' }
    });
  }
};

const CONTROL_HTML = `<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>CLOCK 远程控制</title>
<script src="https://cdn.jsdelivr.net/npm/mqtt@5/dist/mqtt.min.js"></script>
<style>
* { box-sizing: border-box; margin: 0; padding: 0; }
body { font-family: "Courier New", monospace; background: #E0E0E0; color: #404040; padding: 12px; }
.container { max-width: 560px; margin: 0 auto; }
.tabs { display: flex; flex-wrap: wrap; background: #FFF; border: 2px solid #808080; margin-bottom: 8px; }
.tab { flex: 1; min-width: 60px; padding: 8px 4px; font-size: 10px; text-transform: uppercase; letter-spacing: 1px; background: #D0D0D0; color: #808080; border: none; border-right: 1px solid #808080; cursor: pointer; font-family: inherit; }
.tab:last-child { border-right: none; }
.tab.active { background: #FFF; color: #404040; font-weight: bold; }
.tab:hover { background: #C0C0C0; }
.panel { background: #FFF; border: 2px solid #808080; padding: 12px; margin-bottom: 8px; display: none; }
.panel.active { display: block; }
.panel h2 { font-size: 12px; text-transform: uppercase; color: #808080; border-bottom: 2px solid #C0C0C0; padding-bottom: 6px; margin-bottom: 10px; }
.btn { display: inline-block; padding: 10px 14px; font-size: 13px; background: #808080; color: #FFF; border: 2px solid #808080; cursor: pointer; text-transform: uppercase; letter-spacing: 1px; margin: 3px; text-align: center; font-family: inherit; }
.btn:hover { background: #707070; }
.btn:disabled { background: #C0C0C0; cursor: not-allowed; }
.btn-primary { background: #606060; color: #FFF; }
.btn-sm { font-size: 10px; padding: 4px 8px; margin: 2px; }
.btn-green { background: #2A7A2A; border-color: #2A7A2A; }
.btn-red { background: #AA3030; border-color: #AA3030; }
.input { width: 100%; font-family: inherit; font-size: 13px; border: 2px solid #808080; padding: 6px 8px; }
.form-group { margin-bottom: 8px; }
.form-group label { display: block; font-size: 10px; color: #808080; text-transform: uppercase; margin-bottom: 3px; }
.status { font-size: 11px; color: #808080; text-align: center; padding: 6px; }
.status.online { color: #00AA00; }
.status.offline { color: #CC2200; }
.status-row { display: flex; justify-content: space-between; padding: 3px 0; font-size: 12px; }
.status-row .label { color: #808080; }
.status-row .value { color: #404040; font-weight: bold; }
.anim-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 4px; }
.anim-grid .btn { padding: 8px; font-size: 11px; margin: 0; }
.config-row { display: flex; align-items: center; gap: 6px; margin-bottom: 6px; }
.config-row label { font-size: 10px; color: #808080; text-transform: uppercase; min-width: 70px; }

.tube-row { display: flex; gap: 8px; justify-content: center; margin: 8px 0 16px; flex-wrap: wrap; }
.tube { position: relative; width: 72px; height: 130px; flex-shrink: 0; }
.tube-label { position: absolute; bottom: -16px; left: 0; right: 0; text-align: center; font-size: 10px; color: #808080; }
.seg { position: absolute; background: #D0D0D0; cursor: pointer; transition: background 0.1s; border-radius: 2px; }
.seg.on { background: #CC2200; }
.seg:hover { opacity: 0.8; }
.seg-h { top: 6px; left: 18px; width: 36px; height: 7px; }
.seg-g { top: 16px; right: 6px; width: 7px; height: 32px; }
.seg-f { bottom: 32px; right: 6px; width: 7px; height: 32px; }
.seg-e { bottom: 6px; left: 18px; width: 36px; height: 7px; }
.seg-d { bottom: 32px; left: 6px; width: 7px; height: 32px; }
.seg-c { top: 16px; left: 6px; width: 7px; height: 32px; }
.seg-b { top: 54px; left: 18px; width: 36px; height: 7px; }
.seg-a { top: 54px; left: 57px; width: 9px; height: 7px; }
.hex-display { font-size: 10px; color: #808080; text-align: center; margin: 4px 0; font-family: monospace; }
.editor-row { display: flex; gap: 6px; flex-wrap: wrap; align-items: center; margin: 6px 0; }
.editor-row .input { flex: 1; min-width: 80px; }

.frame-list { margin: 6px 0; max-height: 300px; overflow-y: auto; border: 1px solid #D0D0D0; }
.frame-item { display: flex; align-items: center; gap: 4px; padding: 4px 6px; border-bottom: 1px solid #E0E0E0; font-size: 11px; flex-wrap: wrap; }
.frame-item .idx { color: #808080; font-size: 9px; width: 16px; }
.frame-item .hex { font-family: monospace; font-size: 9px; color: #808080; flex: 1; min-width: 80px; }
.frame-item input[type="number"] { width: 50px; font-family: monospace; font-size: 10px; border: 2px solid #808080; padding: 2px 4px; }
.add-frame-row { display: flex; gap: 6px; align-items: center; margin: 6px 0; flex-wrap: wrap; }
.add-frame-row select { flex: 1; min-width: 100px; font-family: monospace; font-size: 11px; border: 2px solid #808080; padding: 4px; }

.saved-list { margin-top: 6px; max-height: 250px; overflow-y: auto; }
.saved-item { display: flex; align-items: center; justify-content: space-between; padding: 4px 6px; border-bottom: 1px solid #E0E0E0; font-size: 11px; flex-wrap: wrap; gap: 4px; }
.saved-item .name { flex: 1; min-width: 60px; }
.saved-item .count { font-size: 9px; color: #808080; }
.hint { font-size: 10px; color: #808080; margin: 3px 0; }
.msg-bar { text-align: center; padding: 6px; font-size: 11px; color: #808080; background: #F0F0F0; border: 1px solid #D0D0D0; margin-top: 6px; }
select.input { width: auto; }
</style>
</head>
<body>
<div class="container" id="app">
  <div class="tabs" id="tabBar" style="display:none;">
    <button class="tab active" data-tab="status">Status</button>
    <button class="tab" data-tab="display">Display</button>
    <button class="tab" data-tab="animations">Anims</button>
    <button class="tab" data-tab="patterns">Patterns</button>
    <button class="tab" data-tab="animator">Animator</button>
  </div>

  <div id="loginPanel" class="panel" style="display:block;">
    <h2>请输入密码</h2>
    <div class="form-group">
      <input type="password" id="pwdInput" class="input" placeholder="远程密码">
    </div>
    <button class="btn" onclick="doLogin()">[ 登录 ]</button>
    <div id="loginError" class="status offline" style="display:none;">密码错误</div>
  </div>

  <div id="statusBar" style="display:none;">
    <div class="status" id="mqttStatus">连接中...</div>
  </div>

  <div id="tabPanels" style="display:none;">
    <!-- Status -->
    <div class="panel active" id="panel-status">
      <h2>状态</h2>
      <div class="status-row"><span class="label">WiFi</span><span class="value" id="sWifi">--</span></div>
      <div class="status-row"><span class="label">IP</span><span class="value" id="sIp">--</span></div>
      <div class="status-row"><span class="label">模式</span><span class="value" id="sMode">--</span></div>
      <div class="status-row"><span class="label">亮度</span><span class="value" id="sBri">--</span></div>
      <div class="status-row"><span class="label">夜间</span><span class="value" id="sNight">--</span></div>
    </div>

    <!-- Display -->
    <div class="panel" id="panel-display">
      <h2>数码管</h2>
      <div class="form-group">
        <label>显示数字 (0-9999)</label>
        <input type="number" id="numInput" class="input" min="0" max="9999" value="1234" style="font-size:20px;text-align:center;">
      </div>
      <button class="btn" onclick="send('display_number',{number:+document.getElementById('numInput').value})">[ 发送数字 ]</button>
      <button class="btn" onclick="send('recover_time',{})">[ 恢复时间 ]</button>
      <button class="btn" onclick="send('toggle_power',{})">[ 开关屏幕 ]</button>

      <h2>亮度</h2>
      <input type="range" id="briSlider" min="0" max="100" value="100" style="width:100%;" oninput="document.getElementById('briVal').textContent=this.value+'%'">
      <div style="display:flex;justify-content:space-between;margin-top:4px;">
        <span class="status">0%</span>
        <span class="status" id="briVal">100%</span>
        <span class="status">100%</span>
      </div>
      <button class="btn" onclick="send('set_brightness',{brightness:+document.getElementById('briSlider').value})">[ 设置亮度 ]</button>

      <h2>夜间模式</h2>
      <div class="config-row"><label>自动关屏</label><input type="checkbox" id="nightEn" onchange="saveNight()" style="width:18px;height:18px;"></div>
      <div class="config-row">
        <label>开始</label>
        <select id="nightStart" onchange="saveNight()" class="input" style="flex:1;">
          <option value="20">20:00</option><option value="21">21:00</option><option value="22">22:00</option><option value="23">23:00</option><option value="0">00:00</option>
        </select>
        <label>结束</label>
        <select id="nightEnd" onchange="saveNight()" class="input" style="flex:1;">
          <option value="5">05:00</option><option value="6">06:00</option><option value="7">07:00</option><option value="8">08:00</option>
        </select>
      </div>
    </div>

    <!-- Animations -->
    <div class="panel" id="panel-animations">
      <h2>内置动画</h2>
      <div class="anim-grid">
        <button class="btn" onclick="send('play_animation',{type:0})">Sunshine</button>
        <button class="btn" onclick="send('play_animation',{type:1})">Raining</button>
        <button class="btn" onclick="send('play_animation',{type:2})">Love</button>
        <button class="btn" onclick="send('play_animation',{type:3})">Smile</button>
        <button class="btn" onclick="send('play_animation',{type:4})">Sad</button>
        <button class="btn" onclick="send('play_animation',{type:5})">Nol</button>
      </div>
    </div>

    <!-- Pattern Editor -->
    <div class="panel" id="panel-patterns">
      <h2>图案编辑</h2>
      <p class="hint">点击段点亮/熄灭 · 实物布局: h(上横) g(右上竖) f(右下竖) e(下横) d(左下竖) c(左上竖) b(中横) a(特殊横)</p>
      <div class="tube-row" id="ptubeRow"></div>
      <div class="hex-display" id="phexDisplay">FF FF FF FF</div>
      <div class="editor-row">
        <input type="text" id="pname" class="input" placeholder="图案名称">
        <button class="btn btn-sm" onclick="pSave()">[ 保存 ]</button>
        <button class="btn btn-sm btn-primary" onclick="pShow()">[ 显示 ]</button>
      </div>
      <h2>已保存图案</h2>
      <div class="saved-list" id="psavedList"></div>
    </div>

    <!-- Animator -->
    <div class="panel" id="panel-animator">
      <h2>动画编辑</h2>
      <div class="frame-list" id="aframeList">
        <div class="frame-item" style="color:#C0C0C0;">暂无帧，从下方添加</div>
      </div>
      <div class="add-frame-row">
        <select id="apatternSelect"><option value="">-- 选择图案 --</option></select>
        <input type="number" id="aframeDuration" value="500" min="50" max="10000" style="width:55px;">
        <span style="font-size:10px;color:#808080;">ms</span>
        <button class="btn btn-sm" onclick="aAddFrame()">[ + ]</button>
      </div>
      <div class="editor-row">
        <input type="text" id="aname" class="input" placeholder="动画名称">
        <button class="btn btn-sm" onclick="aSave()">[ 保存 ]</button>
        <button class="btn btn-sm btn-primary" onclick="aPlay()">[ ▶ 播放 ]</button>
      </div>
      <h2>已保存动画</h2>
      <div class="saved-list" id="asavedList"></div>
    </div>
  </div>

  <div class="msg-bar" id="msgBar">就绪</div>
</div>

<script>
const MQTT_BROKER = 'wss://broker.emqx.io:8084/mqtt';
let PASSWORD = localStorage.getItem('clock_pwd') || '';
let mqttClient = null;
let patternsCache = [];
let animationsCache = [];

// ===== Pattern editor state =====
const SEG_BITS = {a:0,b:1,c:2,d:3,e:4,f:5,g:6,h:7};
const segNames = ['a','b','c','d','e','f','g','h'];
let ptubeData = [0xFF, 0xFF, 0xFF, 0xFF];
let pEditingId = 0;

// ===== Animation editor state =====
let aframes = [];

// ===== Tab switching =====
function switchTab(tabId) {
  document.querySelectorAll('.tab').forEach(t => t.classList.toggle('active', t.dataset.tab === tabId));
  document.querySelectorAll('.panel').forEach(p => p.classList.toggle('active', p.id === 'panel-' + tabId));
}

document.addEventListener('click', function(e) {
  const tab = e.target.closest('.tab');
  if (tab) switchTab(tab.dataset.tab);
});

// ===== Auth =====
async function api(path, opts) {
  const res = await fetch(path, {
    ...opts,
    headers: { ...opts?.headers, 'X-Password': PASSWORD, 'Content-Type': 'application/json' }
  });
  return res.json();
}

function setMqttStatus(online, text) {
  const el = document.getElementById('mqttStatus');
  el.textContent = text || (online ? '时钟在线' : '时钟离线');
  el.className = 'status ' + (online ? 'online' : 'offline');
}

// ===== MQTT =====
function connectMQTT() {
  if (mqttClient) return;
  const clientId = 'web-' + Date.now().toString(36) + '-' + Math.random().toString(36).slice(2, 8);
  mqttClient = mqtt.connect(MQTT_BROKER, { clientId, clean: true });

  mqttClient.on('connect', () => {
    setMqttStatus(true);
    const topic = 'clock/' + PASSWORD + '/status';
    mqttClient.subscribe(topic, (err) => {
      if (err) console.error('订阅状态失败', err);
    });
    pRefresh();
    aRefresh();
  });

  mqttClient.on('message', (topic, message) => {
    try {
      const d = JSON.parse(message.toString());
      if (d._resp === 'patterns') {
        patternsCache = d.items || [];
        pRenderSaved();
        aPopulatePatterns();
        return;
      }
      if (d._resp === 'animations') {
        animationsCache = d.items || [];
        aRenderSaved();
        return;
      }
      document.getElementById('sWifi').textContent = d.wifiState || '--';
      document.getElementById('sIp').textContent = d.ip || '--';
      const modes = ['TIME','WEATHER','NUMBER','OFF','ANIM','PATTERN','ANIM_PLAY'];
      document.getElementById('sMode').textContent = modes[d.displayMode] || '--';
      document.getElementById('sBri').textContent = (d.brightness || 0) + '%';
      const nightStr = d.nightEnabled ? d.nightStart + ':00-' + d.nightEnd + ':00' : '关';
      document.getElementById('sNight').textContent = nightStr;
      document.getElementById('briSlider').value = d.brightness || 0;
      document.getElementById('briVal').textContent = (d.brightness || 0) + '%';
      document.getElementById('nightEn').checked = d.nightEnabled || false;
      document.getElementById('nightStart').value = d.nightStart || 23;
      document.getElementById('nightEnd').value = d.nightEnd || 7;
    } catch(e) { console.error(e); }
  });

  mqttClient.on('close', () => {
    setMqttStatus(false, 'MQTT 断开');
    mqttClient = null;
    setTimeout(connectMQTT, 3000);
  });

  mqttClient.on('error', () => { setMqttStatus(false, 'MQTT 错误'); });
  mqttClient.on('offline', () => { setMqttStatus(false, 'MQTT 离线'); });
}

async function doLogin() {
  const pwd = document.getElementById('pwdInput').value;
  PASSWORD = pwd;
  try {
    const r = await api('/api/auth', { method: 'POST' });
    if (r.success) {
      localStorage.setItem('clock_pwd', pwd);
      document.getElementById('loginPanel').style.display = 'none';
      document.getElementById('tabBar').style.display = 'flex';
      document.getElementById('statusBar').style.display = 'block';
      document.getElementById('tabPanels').style.display = 'block';
      connectMQTT();
      pInit();
      aInit();
      msg('已登录');
    } else {
      document.getElementById('loginError').style.display = 'block';
    }
  } catch(e) {
    document.getElementById('loginError').style.display = 'block';
  }
}

function send(cmd, data) {
  if (!mqttClient || !mqttClient.connected) {
    msg('MQTT 未连接');
    return;
  }
  const topic = 'clock/' + PASSWORD + '/cmd';
  mqttClient.publish(topic, JSON.stringify({ cmd, data, time: Date.now() }));
  msg('已发送: ' + cmd);
}

function saveNight() {
  send('set_night_config', {
    enabled: document.getElementById('nightEn').checked,
    start: parseInt(document.getElementById('nightStart').value),
    end: parseInt(document.getElementById('nightEnd').value)
  });
}

function msg(text) {
  document.getElementById('msgBar').textContent = text;
}

// ===== Pattern Editor =====
function pBuildTube(idx) {
  const tube = document.createElement('div');
  tube.className = 'tube';
  segNames.forEach(s => {
    const div = document.createElement('div');
    div.className = 'seg seg-' + s;
    div.dataset.tube = idx;
    div.dataset.seg = s;
    div.addEventListener('click', () => pToggle(idx, s));
    tube.appendChild(div);
  });
  const label = document.createElement('div');
  label.className = 'tube-label';
  label.textContent = 'T' + idx;
  tube.appendChild(label);
  return tube;
}

function pToggle(tube, seg) {
  ptubeData[tube] ^= (1 << SEG_BITS[seg]);
  pRender();
  pUpdateHex();
}

function pRender() {
  document.querySelectorAll('#panel-patterns .tube').forEach((tube, idx) => {
    const val = ptubeData[idx];
    tube.querySelectorAll('.seg').forEach(seg => {
      const bit = SEG_BITS[seg.dataset.seg];
      seg.classList.toggle('on', !(val & (1 << bit)));
    });
  });
}

function pUpdateHex() {
  const hex = ptubeData.map(v => v.toString(16).toUpperCase().padStart(2, '0')).join(' ');
  document.getElementById('phexDisplay').textContent = hex;
}

function pSetData(data) {
  ptubeData = [...data];
  pRender();
  pUpdateHex();
}

function pClear() {
  pSetData([0xFF, 0xFF, 0xFF, 0xFF]);
  pEditingId = 0;
  document.getElementById('pname').value = '';
}

function pShow() {
  send('show_pattern', { segments: ptubeData });
}

function pSave() {
  const name = document.getElementById('pname').value.trim();
  if (!name) { msg('请输入图案名称'); return; }
  send('save_pattern', { id: pEditingId, name, data: ptubeData });
  pEditingId = 0;
  document.getElementById('pname').value = '';
  msg('图案保存中...');
}

function pLoad(id) {
  const p = patternsCache.find(x => x.id === id);
  if (p) {
    pSetData(p.data);
    pEditingId = id;
    document.getElementById('pname').value = p.name;
    switchTab('patterns');
  }
}

function pDelete(id) {
  if (!confirm('删除此图案？')) return;
  send('delete_pattern', { id });
}

function pPlay(id) {
  const p = patternsCache.find(x => x.id === id);
  if (p) send('show_pattern', { segments: p.data });
}

function pRefresh() {
  send('get_patterns', {});
}

function pRenderSaved() {
  const container = document.getElementById('psavedList');
  container.innerHTML = '';
  if (!patternsCache || patternsCache.length === 0) {
    container.innerHTML = '<div class="saved-item" style="color:#C0C0C0;">暂无图案</div>';
    return;
  }
  patternsCache.forEach(p => {
    const div = document.createElement('div');
    div.className = 'saved-item';
    const hex = p.data.map(v => v.toString(16).toUpperCase().padStart(2,'0')).join(' ');
    div.innerHTML = '<span class="name">' + p.name + '</span>'
      + '<span class="hex" style="font-family:monospace;font-size:9px;color:#808080;">' + hex + '</span>'
      + '<div><button class="btn btn-sm" onclick="pLoad(' + p.id + ')">[ 编辑 ]</button>'
      + '<button class="btn btn-sm" onclick="pPlay(' + p.id + ')">[ 显示 ]</button>'
      + '<button class="btn btn-sm btn-red" onclick="pDelete(' + p.id + ')">[ × ]</button></div>';
    container.appendChild(div);
  });
}

function pInit() {
  const row = document.getElementById('ptubeRow');
  row.innerHTML = '';
  for (let i = 0; i < 4; i++) row.appendChild(pBuildTube(i));
  pRender();
}

// ===== Animation Editor =====
function aPopulatePatterns() {
  const sel = document.getElementById('apatternSelect');
  if (!sel) return;
  sel.innerHTML = '<option value="">-- 选择图案 --</option>';
  (patternsCache || []).forEach(p => {
    const opt = document.createElement('option');
    opt.value = p.id;
    opt.textContent = p.name;
    opt.dataset.data = JSON.stringify(p.data);
    sel.appendChild(opt);
  });
}

function aAddFrame() {
  const sel = document.getElementById('apatternSelect');
  if (!sel.value) { msg('请选择一个图案'); return; }
  const opt = sel.options[sel.selectedIndex];
  const data = JSON.parse(opt.dataset.data);
  const duration = parseInt(document.getElementById('aframeDuration').value) || 500;
  aframes.push({ data, duration });
  aRenderFrames();
}

function aRemoveFrame(idx) {
  aframes.splice(idx, 1);
  aRenderFrames();
}

function aMoveFrame(idx, dir) {
  const to = idx + dir;
  if (to < 0 || to >= aframes.length) return;
  [aframes[idx], aframes[to]] = [aframes[to], aframes[idx]];
  aRenderFrames();
}

function aRenderFrames() {
  const container = document.getElementById('aframeList');
  if (aframes.length === 0) {
    container.innerHTML = '<div class="frame-item" style="color:#C0C0C0;">暂无帧，从下方添加</div>';
    return;
  }
  container.innerHTML = '';
  aframes.forEach((f, i) => {
    const div = document.createElement('div');
    div.className = 'frame-item';
    const hex = f.data.map(v => v.toString(16).toUpperCase().padStart(2,'0')).join(' ');
    div.innerHTML = '<span class="idx">#' + (i+1) + '</span>'
      + '<span class="hex">' + hex + '</span>'
      + '<input type="number" class="dur-input" value="' + f.duration + '" min="50" max="10000" data-idx="' + i + '">'
      + '<span style="font-size:9px;color:#808080;">ms</span>'
      + '<button class="btn btn-sm" onclick="aMoveFrame(' + i + ',-1)">[ ▲ ]</button>'
      + '<button class="btn btn-sm" onclick="aMoveFrame(' + i + ',1)">[ ▼ ]</button>'
      + '<button class="btn btn-sm btn-red" onclick="aRemoveFrame(' + i + ')">[ × ]</button>';
    container.appendChild(div);
  });
  container.querySelectorAll('.dur-input').forEach(inp => {
    inp.addEventListener('change', function() {
      aframes[parseInt(this.dataset.idx)].duration = parseInt(this.value) || 500;
    });
  });
}

function aPlay() {
  if (aframes.length === 0) { msg('至少添加一帧'); return; }
  const frames = aframes.map(f => ({ data: f.data, duration: f.duration }));
  send('play_animation_frames', { frames });
}

function aSave() {
  const name = document.getElementById('aname').value.trim();
  if (!name) { msg('请输入动画名称'); return; }
  if (aframes.length === 0) { msg('至少添加一帧'); return; }
  const frames = aframes.map(f => ({ data: f.data, duration: f.duration }));
  send('save_animation', { id: aEditingId || 0, name, frames });
  aEditingId = 0;
  document.getElementById('aname').value = '';
  aframes = [];
  aRenderFrames();
  msg('动画保存中...');
}
let aEditingId = 0;

function aLoad(id) {
  const a = animationsCache.find(x => x.id === id);
  if (a) {
    aframes = a.frames.map(f => ({ data: [...f.data], duration: f.duration }));
    aRenderFrames();
    aEditingId = id;
    document.getElementById('aname').value = a.name;
    switchTab('animator');
  }
}

function aDelete(id) {
  if (!confirm('删除此动画？')) return;
  send('delete_animation', { id });
}

function aPlaySaved(id) {
  const a = animationsCache.find(x => x.id === id);
  if (a) send('play_animation_frames', { frames: a.frames });
}

function aRefresh() {
  send('get_animations', {});
}

function aRenderSaved() {
  const container = document.getElementById('asavedList');
  container.innerHTML = '';
  if (!animationsCache || animationsCache.length === 0) {
    container.innerHTML = '<div class="saved-item" style="color:#C0C0C0;">暂无动画</div>';
    return;
  }
  animationsCache.forEach(a => {
    const div = document.createElement('div');
    div.className = 'saved-item';
    div.innerHTML = '<span class="name">' + a.name + '</span>'
      + '<span class="count">' + (a.frames ? a.frames.length : 0) + '帧</span>'
      + '<div><button class="btn btn-sm" onclick="aLoad(' + a.id + ')">[ 编辑 ]</button>'
      + '<button class="btn btn-sm" onclick="aPlaySaved(' + a.id + ')">[ ▶ ]</button>'
      + '<button class="btn btn-sm btn-red" onclick="aDelete(' + a.id + ')">[ × ]</button></div>';
    container.appendChild(div);
  });
}

function aInit() {
  aPopulatePatterns();
}

// ===== Auto login =====
(async () => {
  if (PASSWORD) {
    const r = await api('/api/auth', { method: 'POST' });
    if (r.success) {
      document.getElementById('loginPanel').style.display = 'none';
      document.getElementById('tabBar').style.display = 'flex';
      document.getElementById('statusBar').style.display = 'block';
      document.getElementById('tabPanels').style.display = 'block';
      connectMQTT();
      pInit();
      aInit();
      msg('已自动登录');
    } else {
      document.getElementById('loginPanel').style.display = 'block';
    }
  } else {
    document.getElementById('loginPanel').style.display = 'block';
  }
})();
</script>
</body>
</html>`;
