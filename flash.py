#!/usr/bin/env python3
"""MQTT flash notification debug script.

Usage:
    python flash.py start <topic>     # Send flash_start to <topic>
    python flash.py stop  <topic>     # Send flash_stop to <topic>

Examples:
    python flash.py start clock/MyPassword/cmd
    python flash.py stop  clock/MyPassword/cmd

Topic format should match your device's MQTT configuration:
    clock/{password}/cmd
"""

import sys
import paho.mqtt.client as mqtt

BROKER = "broker.emqx.io"
PORT = 1883

def flash_start(topic: str):
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.connect(BROKER, PORT, 10)
    client.publish(topic, '{"cmd":"flash_start"}')
    client.disconnect()
    print(f"[MQTT] flash_start sent to {topic}")

def flash_stop(topic: str):
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.connect(BROKER, PORT, 10)
    client.publish(topic, '{"cmd":"flash_stop"}')
    client.disconnect()
    print(f"[MQTT] flash_stop sent to {topic}")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)

    cmd = sys.argv[1]
    topic = sys.argv[2]

    if cmd == "start":
        flash_start(topic)
    elif cmd == "stop":
        flash_stop(topic)
    else:
        print(f"Unknown command: {cmd}")
        print(__doc__)
        sys.exit(1)
