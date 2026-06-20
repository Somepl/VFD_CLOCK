/**
 * CLOCK — Cloudflare Worker 外网代理
 *
 * KV Namespace: CLOCK_CACHE (必须绑定)
 * Environment:  PASSWORD (远程访问密码)
 *
 * 架构:
 *   ESP32 ——HTTP POST telemetry──→ Worker ——KV──→ 缓存 status/config
 *   Browser ——GET /api/*──────────→ Worker ←─KV── 读取缓存
 *   Browser ——POST /api/* (命令)──→ Worker ——KV──→ 命令队列
 *   ESP32  ——GET /api/poll───────→ Worker ←─KV── 取命令并执行
 *
 * 静态文件走 GitHub Pages，Worker 只做 API 代理。
 * 前端通过 ?worker=<WORKER_URL> 切换远程模式。
 */

const API_AUTH_HEADER = 'X-Password';

// ===== 工具函数 =====

function json(data, status = 200) {
  return new Response(JSON.stringify(data), {
    status,
    headers: { 'Content-Type': 'application/json', 'Access-Control-Allow-Origin': '*' }
  });
}

function corsHeaders() {
  return {
    'Access-Control-Allow-Origin': '*',
    'Access-Control-Allow-Methods': 'GET, POST, DELETE, OPTIONS',
    'Access-Control-Allow-Headers': 'Content-Type, X-Password',
  };
}

function checkAuth(request, env) {
  const PWD = env.PASSWORD || '';
  if (!PWD) return true; // no password = open
  const auth = request.headers.get(API_AUTH_HEADER) || '';
  return auth === PWD;
}

// ===== KV 缓存键 =====

const KV_PREFIX = 'cache:';
const CMD_PREFIX = 'cmd:';
const COUNTER_KEY = 'cmd:counter';

function cacheKey(path) { return KV_PREFIX + path; }
function cmdKey(id) { return CMD_PREFIX + id; }

// ===== 命令队列 =====

async function pushCommand(env, path, bodyJson) {
  const counter = await env.CLOCK_CACHE.get(COUNTER_KEY, { type: 'number' }) || 0;
  const id = counter + 1;
  await env.CLOCK_CACHE.put(COUNTER_KEY, id);
  const key = cmdKey(id);
  await env.CLOCK_CACHE.put(key, JSON.stringify({ path, body: bodyJson }), {
    expirationTtl: 60 // 命令60秒未消费则过期
  });
  return id;
}

async function popCommand(env) {
  const list = await env.CLOCK_CACHE.list({ prefix: CMD_PREFIX });
  const keys = list.keys.sort((a, b) => a.name.localeCompare(b.name)); // 按ID升序
  if (keys.length === 0) return null;
  const first = keys[0];
  if (first.name === COUNTER_KEY) return null;
  const val = await env.CLOCK_CACHE.get(first.name);
  await env.CLOCK_CACHE.delete(first.name);
  if (!val) return null;
  try { return JSON.parse(val); } catch { return null; }
}

// ===== 路由处理 =====

async function handleAuth(request, env) {
  const PWD = env.PASSWORD || '';
  try {
    const body = await request.json();
    const ok = body.password === PWD;
    return json({ success: ok }, ok ? 200 : 401);
  } catch {
    return json({ success: false, error: 'bad request' }, 400);
  }
}

async function handleTelemetry(request, env) {
  try {
    const data = await request.json();
    if (data.status) {
      await env.CLOCK_CACHE.put(cacheKey('/api/status'), JSON.stringify(data.status), { expirationTtl: 30 });
    }
    if (data.config) {
      await env.CLOCK_CACHE.put(cacheKey('/api/config'), JSON.stringify(data.config), { expirationTtl: 30 });
    }
    if (data.patterns) {
      await env.CLOCK_CACHE.put(cacheKey('/api/patterns'), JSON.stringify(data.patterns), { expirationTtl: 60 });
    }
    if (data.animations) {
      await env.CLOCK_CACHE.put(cacheKey('/api/animations'), JSON.stringify(data.animations), { expirationTtl: 60 });
    }
    if (data.builtins) {
      await env.CLOCK_CACHE.put(cacheKey('/api/animations/builtin'), JSON.stringify(data.builtins), { expirationTtl: 60 });
    }
    if (data.wifiSaved) {
      await env.CLOCK_CACHE.put(cacheKey('/api/wifi/saved'), JSON.stringify(data.wifiSaved), { expirationTtl: 30 });
    }
    return json({ success: true });
  } catch {
    return json({ success: false, error: 'bad telemetry' }, 400);
  }
}

async function handlePoll(request, env) {
  const cmd = await popCommand(env);
  if (!cmd) return json({ command: null });
  return json({ command: cmd });
}

async function handleGet(request, url, env) {
  const path = url.pathname;
  const cached = await env.CLOCK_CACHE.get(cacheKey(path));
  if (cached) {
    return new Response(cached, {
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
  // 没有缓存：返回空数据 + 触发命令让ESP32上报
  return json({ cached: false, data: null });
}

async function handlePost(request, url, env) {
  const path = url.pathname;
  let body = null;
  try { body = await request.json(); } catch {}
  const cmdId = await pushCommand(env, path, body);
  return json({ success: true, cmdId });
}

async function handleDelete(request, url, env) {
  const path = url.pathname;
  let body = null;
  try { body = await request.json(); } catch {}
  const cmdId = await pushCommand(env, path, body);
  return json({ success: true, cmdId });
}

// ===== ESP32 本地直连代理（可选，用于外网页面通过Worker转发到ESP32公网IP） =====

async function handleProxy(request, url, env) {
  // 如果配置了 TARGET_URL（ESP32公网地址），直接转发
  const target = env.TARGET_URL || '';
  if (!target) return json({ error: 'proxy not configured' }, 502);
  const targetUrl = target + url.pathname + url.search;
  const headers = new Headers(request.headers);
  headers.delete('Host');
  try {
    const resp = await fetch(targetUrl, {
      method: request.method,
      headers,
      body: (request.method === 'POST' || request.method === 'DELETE') ? await request.text() : undefined
    });
    return resp;
  } catch {
    return json({ error: 'proxy failed' }, 502);
  }
}

// ===== 主入口 =====

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const path = url.pathname;

    // CORS preflight
    if (request.method === 'OPTIONS') {
      return new Response('', { headers: corsHeaders() });
    }

    // Auth (除 telemetry 外所有 /api/* 需要验证)
    if (path.startsWith('/api/') && path !== '/api/telemetry' && path !== '/api/auth') {
      if (!checkAuth(request, env)) {
        return json({ success: false, error: 'unauthorized' }, 401);
      }
    }

    // === API 路由 ===

    // 鉴权
    if (path === '/api/auth' && request.method === 'POST') {
      return handleAuth(request, env);
    }

    // ESP32 上报遥测数据
    if (path === '/api/telemetry' && request.method === 'POST') {
      return handleTelemetry(request, env);
    }

    // ESP32 轮询命令
    if (path === '/api/poll' && request.method === 'GET') {
      return handlePoll(request, env);
    }

    // 代理模式（直接转发到 ESP32 公网 IP，需配置 TARGET_URL）
    if (path.startsWith('/api/proxy/')) {
      return handleProxy(request, url, env);
    }

    // GET 读取缓存
    if (path.startsWith('/api/') && request.method === 'GET') {
      return handleGet(request, url, env);
    }

    // POST 写入命令队列
    if (path.startsWith('/api/') && request.method === 'POST') {
      return handlePost(request, url, env);
    }

    // DELETE 删除
    if (path.startsWith('/api/') && request.method === 'DELETE') {
      return handleDelete(request, url, env);
    }

    return json({ error: 'not found' }, 404);
  }
};
