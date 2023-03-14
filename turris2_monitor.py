#!/usr/bin/env python
import paho.mqtt.client as mqtt
import time
import json
import ubus
import shutil
import subprocess


mqtt_topic = "turris2/monitor"

monitor_dict = {"uptime": 0, "load": 0, "temperature": 0, "memory_usage": 100, "uplink_speed": 0, "uplink_strength": 0, "disk_usage": 0, "disk_free": 0}

# initialize MQTT connection
client = mqtt.Client("turris2_monitor")

time.sleep(60)
disconnected_counter = 0

while True:
        ubus.connect()
        sysinfo_json = ubus.call("system", "info", {})
        iwinfo_json = ubus.call("iwinfo", "info", { "device":"wlan1"})
        ubus.disconnect()

        sysinfo = sysinfo_json[0]
        iwinfo = iwinfo_json[0]

        #print(sysinfo)
        #print(iwinfo)

        total, used, free = shutil.disk_usage("/")
        disk_usage = round(100 * (used / total))
        disk_free = round(free // (2**30), 1)

        uptime = sysinfo["uptime"]
        load = round(sysinfo["load"][1] / 65536.0, 2)
        memory_usage = 100 - round(100 * sysinfo["memory"]["available"] / sysinfo["memory"]["total"])

        uplink_strength = iwinfo.get("signal", "0")
        uplink_speed = round(iwinfo.get("bitrate", "-120") / 1000, 1)

        with open("/sys/class/thermal/thermal_zone0/temp",'r') as f:
                temp = f.read()
        temp = round(int(temp) / 1000)
        #print(temp)

        monitor_dict["uptime"] = uptime
        monitor_dict["load"] = load
        monitor_dict["temperature"] = temp
        monitor_dict["memory_usage"] = memory_usage
        monitor_dict["uplink_strength"] = uplink_strength
        monitor_dict["uplink_speed"] = uplink_speed
        monitor_dict["disk_usage"] = disk_usage
        monitor_dict["disk_free"] = disk_free
        #print(monitor_dict)
        mqtt_connected = 1
        try:
                client.connect("xxx.xxx.xx.xxx")
        except:
                mqtt_connected = 0
                disconnected_counter = disconnected_counter + 1
        if mqtt_connected:
                infot = client.publish(mqtt_topic, json.dumps(monitor_dict).encode('utf-8'), 0, True)
                infot.wait_for_publish()
                client.disconnect()
        else:
                if disconnected_counter > 2:
                        subprocess.run(['/sbin/wifi', 'down', 'radio2'])
                        time.sleep(10)
                        subprocess.run(['/sbin/wifi', 'up', 'radio2'])
                        disconnected_counter = 0

        time.sleep(300)

