/**
 * script.js — 共享前端工具函数
 *
 * 支持本地模式（直接调 ESP32）和远程模式（通过 Worker 代理）
 * 远程模式配置：window._API_BASE = Worker URL, window._API_PWD = 密码
 */

const API_BASE = window._API_BASE || '';
const API_PWD = window._API_PWD || '';

async function fetchJson(url, method = 'GET', body = null) {
    const options = {
        method: method,
        headers: {
            'Content-Type': 'application/json',
        },
    };

    if (API_PWD) {
        options.headers['X-Password'] = API_PWD;
    }

    if (body && (method === 'POST' || method === 'DELETE')) {
        options.body = JSON.stringify(body);
    }

    const response = await fetch(API_BASE + url, options);

    if (!response.ok) {
        throw new Error('HTTP ' + response.status);
    }

    return await response.json();
}
