# -*- coding: utf-8 -*-
"""Fix UTF-8 corruption in all source files caused by PowerShell encoding roundtrip."""

import os
import re

SRC_DIR = "src"

# Map of corrupted strings -> correct strings for each file
# The corruption happened when PowerShell read UTF-8 as ANSI and wrote back,
# destroying multi-byte Chinese characters. I reconstruct from context.

REPLACEMENTS = {
    # button_handler.cpp
    "Log.println(F(\"[����] ��ʼ����ɨ?..\"));": 'Log.println(F("[按键] 初始化开始..."));',
    "Log.println(F(\"[����] ��ʼ����ɣ��Զ�У׼����3������?));":
        'Log.println(F("[按键] 初始化完成（自动校准将在3秒后进行）"));',
    "Log.println(F(\"[����] �Զ�У׼��?..\"));": 'Log.println(F("[按键] 自动校准开始..."));',
    "Log.printf(\"[����] ����%d: ����=%d, ??%d\\n\", i + 1, avg, thr);":
        'Log.printf("[按键] 按键%d: 基线=%d, 阈值=%d\\n", i + 1, avg, thr);',
    "Log.printf(\"[����] ����%d (T%d) ??%d\\n\",":
        'Log.printf("[按键] 按键%d (T%d) 阈值=%d\\n",',

    # ntp_sync.cpp
    "Log.println(F(\"[NTP] ��ʼ?..\"));": 'Log.println(F("[NTP] 初始化..."));',
    "Log.printf(\"[NTP] ����? %s, ʱ��: UTC%d\\n\",":
        'Log.printf("[NTP] 服务器: %s, 时区: UTC%d\\n",',
    "Log.println(F(\"[NTP] �ȴ� WiFi ���Ӻ��Զ�ͬ?..\"));":
        'Log.println(F("[NTP] 等待 WiFi 连接后自动同步..."));',
    "Log.println(F(\"[NTP] ��ʼͬ?..\"));": 'Log.println(F("[NTP] 开始同步..."));',
    "Log.println(F(\"[NTP] ͬ���ɹ�?));": 'Log.println(F("[NTP] 同步成功！"));',
    "Log.println(F(\"[NTP] ͬ����ʱ?0�룩\"));": 'Log.println(F("[NTP] 同步超时（0秒）"));',

    # pattern_manager.cpp
    "Log.println(F(\"[PM] LittleFS �Ѿ�?));": 'Log.println(F("[PM] LittleFS 已就绪"));',

    # remote_client.cpp
    "Log.println(F(\"[MQTT] δ���ã��ѽ�?));": 'Log.println(F("[MQTT] 未配置，已禁用"));',

    # wifi_manager.cpp
    "Log.println(F(\"[WiFi] Modem-sleep ����?));": 'Log.println(F("[WiFi] Modem-sleep 已启用"));',
    "Log.println(F(\"[WiFi] �״ο�����δ������������?��ʹ����ҳ��?));":
        'Log.println(F("[WiFi] 首次开机，未配网。长按按键1或使用网页配网"));',
    "Log.println(F(\"[WiFi] AP ģʽ��ʱ?���ӣ����Զ��ر�\"));":
        'Log.println(F("[WiFi] AP 模式超时（3分钟），自动关闭"));',
    "Log.println(F(\"[WiFi] AP ����ʧ��?));": 'Log.println(F("[WiFi] AP 启动失败！"));',
    "Log.println(F(\"[WiFi] ƾ����������������?));": 'Log.println(F("[WiFi] 凭据已清除，请重新配网"));',
    "Log.printf(\"[WiFi] �Ѽ���ƾ? SSID=%s, ����=%d\\n\",":
        'Log.printf("[WiFi] 已加载凭据: SSID=%s, 长度=%d\\n",',
    "Log.printf(\"[WiFi] ƾ���ѱ�? SSID=%s\\n\", ssid);":
        'Log.printf("[WiFi] 凭据已保存: SSID=%s\\n", ssid);',
    "Log.printf(\"[WiFi] ���?s\\n\", ssid);": 'Log.printf("[WiFi] 配网：%s\\n", ssid);',
    "Log.printf(\"[WiFi] ����״? %s (status=%d)\\n\", statusStr, s);":
        'Log.printf("[WiFi] 连接状态: %s (status=%d)\\n", statusStr, s);',
    "Log.println(F(\"[WiFi] ������ɣ�������?..\"));":
        'Log.println(F("[WiFi] 配网完成，正在连接..."));',
    "Log.println(F(\"[WiFi] �ͻ����뿪 AP�����¼�?���ӳ�ʱ\"));":
        'Log.println(F("[WiFi] 客户端离开 AP，重新计时3分钟超时"));',

    # main.cpp
    "Log.println(F(\"  ESP32 �����ʱ?����?..\"));":
        'Log.println(F("  ESP32 数码管时钟 启动中..."));',
    "Log.println(F(\"[ϵͳ] �����ѹ�?));": 'Log.println(F("[系统] 蓝牙已关闭"));',
    "Log.println(F(\"��ʼ����ɣ�������ѭ?));": 'Log.println(F("初始化完成，进入主循环"));',
    'Log.printf("[OTA] ��ʼ��?%s ...\\n", type);':
        'Log.printf("[OTA] 开始更新 %s ...\\n", type);',
    "Log.println(F(\"[����] ����2�̰����л���?));":
        'Log.println(F("[主控] 按键2短按：切换动画"));',
}

def fix_file(filepath):
    with open(filepath, 'r', encoding='utf-8', errors='replace') as f:
        content = f.read()
    
    original = content
    for corrupt, correct in REPLACEMENTS.items():
        if corrupt in content:
            content = content.replace(corrupt, correct)
            print(f"  Fixed: {corrupt[:40]}...")
    
    if content != original:
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(content)
        return True
    return False

def main():
    for fname in sorted(os.listdir(SRC_DIR)):
        if fname.endswith('.cpp') or fname.endswith('.h'):
            fpath = os.path.join(SRC_DIR, fname)
            print(f"\nProcessing {fname}...")
            fixed = fix_file(fpath)
            if not fixed:
                print("  No changes needed (or patterns not found)")

if __name__ == '__main__':
    main()
