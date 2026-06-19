/**
 * web_server.h — 异步网页服务器 + mDNS
 *
 * 职责：
 *   - 从 LittleFS 提供静态网页文件
 *   - 提供 REST API：状态查询、WiFi扫描、配网、数字显示
 *   - mDNS 服务 (clock.local)
 *
 * ESPAsyncWebServer 在后台处理请求，不阻塞 loop()
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>

/** 初始化网页服务器 + mDNS（在 WiFi 连接后调用） */
void web_server_init();

/** 停止网页服务器 + mDNS（在 WiFi 断开时调用） */
void web_server_stop();

/** 启动 mDNS（STA 获取 IP 后调用） */
void web_server_start_mdns();

#endif // WEB_SERVER_H
