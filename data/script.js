/**
 * script.js — 共享前端工具函数
 *
 * 本地模式：直接调 ESP32 HTTP API
 * 远程模式（自动检测 _API_BASE）：
 *   - GET  读 → Worker HTTP（KV 缓存）
 *   - POST/DELETE 写 → MQTT WebSocket 直连（实时）
 *   - MQTT 状态更新 → 缓存用于 GET
 */

const API_BASE = window._API_BASE || '';
const API_PWD = window._API_PWD || '';
const MQTT_BROKER = window._MQTT_BROKER || 'wss://broker.emqx.io:8084/mqtt';

// ===== MQTT 状态 + 缓存 =====
let mqttClient = null;
let mqttReady = false;
const mqttCache = {};

function mqttConnect() {
    if (mqttClient || !API_PWD || typeof mqtt === 'undefined') return;
    const clientId = 'web-' + Date.now().toString(36) + '-' + Math.random().toString(36).slice(2, 8);
    mqttClient = mqtt.connect(MQTT_BROKER, { clientId, clean: true });

    mqttClient.on('connect', () => {
        const topic = 'clock/' + API_PWD + '/status';
        mqttClient.subscribe(topic, (err) => {
            if (err) console.warn('[MQTT] 订阅状态失败', err);
        });
        mqttReady = true;
        console.log('[MQTT] 已连接, 密码:', API_PWD);
    });

    mqttClient.on('message', (topic, message) => {
        try {
            const d = JSON.parse(message.toString());
            // 缓存各类数据
            if (d._resp === 'patterns') {
                mqttCache['/api/patterns'] = d.items || [];
                return;
            }
            if (d._resp === 'animations') {
                mqttCache['/api/animations'] = d.items || [];
                return;
            }
            // 通用状态缓存
            mqttCache['/api/status'] = d;
            mqttCache['/api/config'] = d;
        } catch (e) { /* ignore */ }
    });

    mqttClient.on('close', () => {
        mqttReady = false;
        mqttClient = null;
        setTimeout(mqttConnect, 3000);
    });
    mqttClient.on('error', () => { mqttReady = false; });
    mqttClient.on('offline', () => { mqttReady = false; });
}

// ===== HTTP 到 MQTT 命令映射 =====

const API_MAP = {
    '/api/display/number':         (b) => ({ cmd: 'display_number', data: { number: b.number } }),
    '/api/display/recover':        ()  => ({ cmd: 'recover_time', data: {} }),
    '/api/display/animation':      (b) => ({ cmd: 'play_animation', data: { type: b.type } }),
    '/api/display/anim-play':      (b) => ({ cmd: 'play_animation_frames', data: { frames: b.frames } }),
    '/api/display/pattern':        (b) => ({ cmd: 'show_pattern', data: { segments: b.data } }),
    '/api/config':                 (b) => ({ cmd: 'set_config', data: b }),
    '/api/patterns_POST':          (b) => ({ cmd: 'save_pattern', data: b }),
    '/api/patterns_DELETE':        (b) => ({ cmd: 'delete_pattern', data: { id: b.id } }),
    '/api/animations_POST':        (b) => ({ cmd: 'save_animation', data: b }),
    '/api/animations_DELETE':      (b) => ({ cmd: 'delete_animation', data: { id: b.id } }),
    '/api/animations/builtin_POST':(b) => ({ cmd: 'override_builtin', data: b }),
    '/api/animations/builtin_DELETE':(b)=> ({ cmd: 'restore_builtin', data: { id: b.id } }),
    '/api/restart':                ()  => ({ cmd: 'restart', data: {} }),
};

function urlToMqtt(url, method, body) {
    const key = url + '_' + method;
    if (API_MAP[key]) return API_MAP[key](body);
    if (API_MAP[url]) return API_MAP[url](body);
    // 通用回退：用 path 最后一段作为 cmd
    const parts = url.split('/');
    const cmd = parts[parts.length - 1];
    return { cmd, data: body || {} };
}

async function mqttSend(url, method, body) {
    const { cmd, data } = urlToMqtt(url, method, body);
    if (!mqttClient || !mqttReady) {
        console.warn('[MQTT] 未连接，回退 HTTP');
        return httpFetch(url, method, body);
    }
    return new Promise((resolve, reject) => {
        const topic = 'clock/' + API_PWD + '/cmd';
        const payload = JSON.stringify({ cmd, data, time: Date.now() });
        mqttClient.publish(topic, payload, {}, (err) => {
            if (err) {
                console.warn('[MQTT] 发送失败，回退 HTTP', err);
                httpFetch(url, method, body).then(resolve).catch(reject);
            } else {
                resolve({ success: true });
            }
        });
        // 如果 3 秒没有 MQTT 回调确认，回退 HTTP
        setTimeout(() => {
            if (!mqttReady) {
                httpFetch(url, method, body).then(resolve).catch(reject);
            }
        }, 3000);
    });
}

async function httpFetch(url, method, body) {
    const options = {
        method,
        headers: { 'Content-Type': 'application/json' },
    };
    if (API_PWD) options.headers['X-Password'] = API_PWD;
    if (body && (method === 'POST' || method === 'DELETE')) {
        options.body = JSON.stringify(body);
    }
    const response = await fetch(API_BASE + url, options);
    if (!response.ok) throw new Error('HTTP ' + response.status);
    return response.json();
}

// ===== 公开接口 =====

async function fetchJson(url, method = 'GET', body = null) {
    // 远程模式
    if (API_BASE) {
        // POST/DELETE → MQTT 直连（实时）
        if (method === 'POST' || method === 'DELETE') {
            if (mqttReady) return mqttSend(url, method, body);
            // 尝试连接 MQTT（异步，首次调用时连接）
            if (!mqttClient) mqttConnect();
        }
        // GET → Worker HTTP 缓存
        return httpFetch(url, method, body);
    }
    // 本地模式：直接调 ESP32
    return httpFetch(url, method, body);
}

// ===== 主题切换 =====

function initTheme() {
    const saved = localStorage.getItem('theme');
    const prefersDark = window.matchMedia('(prefers-color-scheme: dark)').matches;

    if (saved === 'dark' || (!saved && prefersDark)) {
        document.documentElement.classList.add('dark');
    }

    const btn = document.createElement('button');
    btn.className = 'theme-toggle';
    btn.setAttribute('aria-label', '切换主题');
    btn.textContent = document.documentElement.classList.contains('dark') ? '☀' : '☾';
    btn.onclick = () => {
        document.documentElement.classList.toggle('dark');
        const isDark = document.documentElement.classList.contains('dark');
        btn.textContent = isDark ? '☀' : '☾';
        localStorage.setItem('theme', isDark ? 'dark' : 'light');
    };

    const header = document.querySelector('header');
    if (header) {
        header.appendChild(btn);
    }
}

if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initTheme);
} else {
    initTheme();
}

// 自动初始化（仅在远程模式下）
if (API_BASE && typeof mqtt !== 'undefined') {
    setTimeout(mqttConnect, 500);
}
