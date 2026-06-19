/**
 * 时钟远程中继服务器 (Node.js)
 *
 * 部署到云服务器：
 *   1. 安装 Node.js: apt install nodejs npm -y
 *   2. 上传本文件到服务器
 *   3. 设置密码: export PASSWORD=你的密码
 *   4. 运行: node relay-server.js
 *   5. 手机访问 http://服务器IP:3000
 *
 * 建议使用 PM2 后台运行：
 *   npm install pm2 -g
 *   pm2 start relay-server.js --name clock-relay
 */

const http = require('http');
const url = require('url');
const fs = require('fs');

const PORT = 3000;
const PASSWORD = process.env.PASSWORD || 'clock123';

// 存储 ESP32 的 WebSocket 连接
let espWS = null;

// ============================================================
// WebSocket 处理（原生，不依赖第三方库）
// ============================================================

function handleWebSocket(req, socket) {
  // 发送 HTTP 101 Switching Protocols
  const key = req.headers['sec-websocket-key'];
  const accept = require('crypto')
    .createHash('sha1')
    .update(key + '258EAFA5-E914-47DA-95CA-5AB5DC11B735')
    .digest('base64');

  socket.write(
    'HTTP/1.1 101 Switching Protocols\r\n' +
    'Upgrade: websocket\r\n' +
    'Connection: Upgrade\r\n' +
    `Sec-WebSocket-Accept: ${accept}\r\n\r\n`
  );

  let buf = Buffer.alloc(0);

  function sendWS(data) {
    const msg = Buffer.from(JSON.stringify(data));
    const len = msg.length;
    let frame;
    if (len < 126) {
      frame = Buffer.alloc(2 + len);
      frame[0] = 0x81; // text, fin
      frame[1] = len;
      msg.copy(frame, 2);
    } else if (len < 65536) {
      frame = Buffer.alloc(4 + len);
      frame[0] = 0x81;
      frame[1] = 126;
      frame.writeUInt16BE(len, 2);
      msg.copy(frame, 4);
    } else {
      frame = Buffer.alloc(10 + len);
      frame[0] = 0x81;
      frame[1] = 127;
      frame.writeBigUInt64BE(BigInt(len), 2);
      msg.copy(frame, 10);
    }
    socket.write(frame);
  }

  socket.on('data', (data) => {
    buf = Buffer.concat([buf, data]);

    while (buf.length >= 2) {
      const opcode = buf[0] & 0x0f;
      const masked = (buf[1] & 0x80) !== 0;
      let payloadLen = buf[1] & 0x7f;
      let offset = 2;

      if (payloadLen === 126) {
        if (buf.length < 4) return;
        payloadLen = buf.readUInt16BE(2);
        offset = 4;
      } else if (payloadLen === 127) {
        if (buf.length < 10) return;
        payloadLen = Number(buf.readBigUInt64BE(2));
        offset = 10;
      }

      if (buf.length < offset + (masked ? 4 : 0) + payloadLen) return;

      let mask = null;
      if (masked) {
        mask = buf.slice(offset, offset + 4);
        offset += 4;
      }

      let payload = buf.slice(offset, offset + payloadLen);
      if (mask) {
        for (let i = 0; i < payload.length; i++) {
          payload[i] ^= mask[i % 4];
        }
      }

      buf = buf.slice(offset + payloadLen);

      if (opcode === 0x08) {
        // Close frame
        socket.end();
        espWS = null;
        console.log('[中继] ESP32 断开连接');
        return;
      }

      if (opcode === 0x09) {
        // Ping → Pong
        const pong = Buffer.alloc(2);
        pong[0] = 0x8A;
        pong[1] = 0;
        socket.write(pong);
        continue;
      }

      if (opcode === 0x01) {
        try {
          const msg = JSON.parse(payload.toString());

          if (msg.type === 'auth' && msg.role === 'esp' && msg.password === PASSWORD) {
            espWS = { send: sendWS, socket };
            console.log('[中继] ESP32 认证成功');
            sendWS({ type: 'auth_ok' });
          } else if (msg.type === 'heartbeat') {
            // 心跳
          }
        } catch (e) {
          console.log('[中继] 消息解析失败:', e.message);
        }
      }
    }
  });

  socket.on('close', () => {
    if (espWS && espWS.socket === socket) {
      espWS = null;
      console.log('[中继] ESP32 断开连接');
    }
  });
}

// ============================================================
// HTTP 服务器
// ============================================================

const server = http.createServer((req, res) => {
  const parsed = url.parse(req.url, true);
  const path = parsed.pathname;

  // CORS
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type, X-Password');

  if (req.method === 'OPTIONS') {
    res.writeHead(204);
    res.end();
    return;
  }

  // WebSocket 升级
  if (req.headers['upgrade'] === 'websocket') {
    handleWebSocket(req, res.socket);
    return;
  }

  const sendJSON = (code, data) => {
    res.writeHead(code, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(data));
  };

  const auth = req.headers['x-password'] || parsed.query.pwd;

  // 手机发送命令
  if (path === '/api/send' && req.method === 'POST') {
    if (auth !== PASSWORD) {
      sendJSON(401, { error: 'Unauthorized' });
      return;
    }
    if (!espWS) {
      sendJSON(503, { error: '时钟离线', online: false });
      return;
    }

    let body = '';
    req.on('data', chunk => body += chunk);
    req.on('end', () => {
      try {
        const cmd = JSON.parse(body);
        espWS.send({
          type: 'command',
          cmd: cmd.cmd,
          data: cmd.data,
          requestId: cmd.requestId || ''
        });
        sendJSON(200, { success: true, online: true });
      } catch (e) {
        sendJSON(400, { error: 'JSON格式错误' });
      }
    });
    return;
  }

  // 状态查询
  if (path === '/api/status') {
    sendJSON(200, { online: espWS !== null });
    return;
  }

  // 控制页面
  if (path === '/') {
    res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
    res.end(CONTROL_HTML);
    return;
  }

  sendJSON(404, { error: 'Not Found' });
});

server.listen(PORT, '0.0.0.0', () => {
  console.log(`[中继] 时钟远程中继服务已启动`);
  console.log(`[中继] 地址: http://0.0.0.0:${PORT}`);
  console.log(`[中继] 手机访问: http://服务器IP:${PORT}`);
  console.log(`[中继] 密码: ${PASSWORD}`);
});

// ============================================================
// 控制页面 HTML
// ============================================================

const CONTROL_HTML = `<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>CLOCK 远程控制</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body { font-family: "Courier New", monospace; background: #E0E0E0; color: #404040; padding: 16px; }
  .container { max-width: 480px; margin: 0 auto; }
  .panel { background: #FFF; border: 2px solid #808080; padding: 16px; margin-bottom: 12px; }
  .panel h2 { font-size: 14px; text-transform: uppercase; color: #808080; border-bottom: 2px solid #C0C0C0; padding-bottom: 8px; margin-bottom: 12px; }
  .btn { display: block; width: 100%; padding: 14px; font-size: 16px; background: #808080; color: #FFF; border: 2px solid #808080; cursor: pointer; text-transform: uppercase; letter-spacing: 2px; margin-bottom: 8px; text-align: center; font-family: inherit; }
  .btn:hover { background: #707070; }
  .btn:disabled { background: #C0C0C0; cursor: not-allowed; }
  .input { width: 100%; font-family: inherit; font-size: 14px; border: 2px solid #808080; padding: 8px 10px; }
  .form-group { margin-bottom: 10px; }
  .form-group label { display: block; font-size: 11px; color: #808080; text-transform: uppercase; margin-bottom: 4px; }
  .status { font-size: 11px; color: #808080; text-align: center; padding: 8px; }
  .status.online { color: #00AA00; }
  .status.offline { color: #CC2200; }
</style>
</head>
<body>
<div class="container">
  <div class="panel" style="text-align:center;">
    <h2>时钟远程控制</h2>
    <div class="status" id="status">检查连接...</div>
  </div>

  <div id="loginPanel" class="panel">
    <h2>请输入密码</h2>
    <div class="form-group">
      <input type="password" id="pwdInput" class="input" placeholder="远程密码">
    </div>
    <button class="btn" onclick="doLogin()">[ 登录 ]</button>
    <div id="loginError" class="status offline" style="display:none;">密码错误</div>
  </div>

  <div id="controlPanel" style="display:none;">
    <div class="panel">
      <h2>数码管</h2>
      <div class="form-group">
        <label>显示数字 (0-9999)</label>
        <input type="number" id="numInput" class="input" min="0" max="9999" value="1234" style="font-size:22px;text-align:center;">
      </div>
      <button class="btn" onclick="send('display_number', {number:+document.getElementById('numInput').value})">[ 发送数字 ]</button>
      <button class="btn" onclick="send('recover_time',{})">[ 恢复时间 ]</button>
      <button class="btn" onclick="send('toggle_power',{})">[ 开关屏幕 ]</button>
    </div>

    <div class="panel">
      <h2>亮度</h2>
      <label>亮度: <span id="briVal">100</span>%</label>
      <input type="range" min="0" max="100" value="100" style="width:100%;"
             onchange="document.getElementById('briVal').textContent=this.value">
      <button class="btn" onclick="send('set_brightness',{brightness:+document.querySelector('input[type=range]').value})" style="margin-top:8px;">[ 设置亮度 ]</button>
    </div>

    <div class="panel" style="text-align:center;">
      <p class="status" id="msg">就绪</p>
    </div>
  </div>
</div>

<script>
let PASSWORD = localStorage.getItem('clock_pwd') || '';

async function api(path, opts) {
  const res = await fetch(path, {
    ...opts,
    headers: { ...opts?.headers, 'X-Password': PASSWORD, 'Content-Type': 'application/json' }
  });
  return res.json();
}

async function checkStatus() {
  try {
    const s = await api('/api/status');
    const el = document.getElementById('status');
    if (s.online) { el.textContent = '时钟在线'; el.className = 'status online'; }
    else { el.textContent = '时钟离线'; el.className = 'status offline'; }
  } catch(e) {}
}

async function doLogin() {
  PASSWORD = document.getElementById('pwdInput').value;
  const s = await api('/api/status');
  if (s.online) {
    localStorage.setItem('clock_pwd', PASSWORD);
    document.getElementById('loginPanel').style.display = 'none';
    document.getElementById('controlPanel').style.display = 'block';
  } else {
    document.getElementById('loginError').style.display = 'block';
  }
}

async function send(cmd, data) {
  const msg = document.getElementById('msg');
  const r = await api('/api/send', { method: 'POST', body: JSON.stringify({ cmd, data, requestId: Date.now().toString(36) }) });
  if (r.success) msg.textContent = '已发送: ' + cmd;
  else msg.textContent = '失败: ' + (r.error || '时钟离线');
}

(async () => {
  const s = await api('/api/status');
  if (s.online && PASSWORD) {
    document.getElementById('loginPanel').style.display = 'none';
    document.getElementById('controlPanel').style.display = 'block';
  }
  setInterval(checkStatus, 5000);
})();
</script>
</body>
</html>`;
