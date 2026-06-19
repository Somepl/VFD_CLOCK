/**
 * script.js — 共享前端工具函数
 *
 * 提供 AJAX 请求封装，所有页面共用
 */

/**
 * 发起 JSON API 请求
 * @param {string} url    - API 路径（如 '/api/status'）
 * @param {string} method - HTTP 方法，默认 'GET'
 * @param {object} body   - 请求体对象（仅 POST），可选
 * @returns {Promise<object>} 解析后的 JSON 响应
 */
async function fetchJson(url, method = 'GET', body = null) {
    const options = {
        method: method,
        headers: {
            'Content-Type': 'application/json',
        },
    };

    if (body && method === 'POST') {
        options.body = JSON.stringify(body);
    }

    const response = await fetch(url, options);

    if (!response.ok) {
        throw new Error('HTTP ' + response.status);
    }

    return await response.json();
}
