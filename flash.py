import paho.mqtt.client as mqtt

BROKER = "broker.emqx.io"
PORT = 1883
TOPIC = "clock/YOUR_MQTT_PASSWORD_HERE/cmd"

def flash_start():
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.connect(BROKER, PORT, 10)
    client.publish(TOPIC, '{"cmd":"flash_start"}')
    client.disconnect()
    print("[MQTT] flash_start sent")

def flash_stop():
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.connect(BROKER, PORT, 10)
    client.publish(TOPIC, '{"cmd":"flash_stop"}')
    client.disconnect()
    print("[MQTT] flash_stop sent")

if __name__ == "__main__":
    import sys
    if len(sys.argv) < 2:
        print("Usage: python flash.py start|stop")
        sys.exit(1)
    if sys.argv[1] == "start":
        flash_start()
    elif sys.argv[1] == "stop":
        flash_stop()
    else:
        print("Unknown command")
