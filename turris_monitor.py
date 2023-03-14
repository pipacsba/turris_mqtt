!/usr/bin/env python
import paho.mqtt.client as mqtt
import time
import json
import ubus
import shutil
import subprocess

debug = 0

if debug == 0:
    mqtt_topic = "turris/monitor"

monitor_dict = {"uptime": 0, "load": 0, "temperature": 0, "memory_usage": 100, "disk_usage": 0, "disk_free": 0, "disk_temperature": 0, "movies": 0, "series": 0, "lxc": "none"}

if debug == 0:
    # initialize MQTT connection
    client = mqtt.Client("turris_monitor")
    time.sleep(60)

while True:
        ubus.connect()
        sysinfo_json = ubus.call("system", "info", {})
        lxc_list = ubus.call("lxc", "list", {})
        #iwinfo_json = ubus.call("iwinfo", "info", { "device":"wlan1"})
        ubus.disconnect()

        #print(sysinfo)
        #print(iwinfo)

        mtotal, mused, mfree = shutil.disk_usage("/mnt/movies")
        movies_usage = round(100 * (mused / mtotal))

        stotal, sused, sfree = shutil.disk_usage("/mnt/series")
        series_usage = round(100 * (sused / stotal))

        total, used, free = shutil.disk_usage("/srv")
        disk_usage = round(100 * (used / total))
        disk_free = round(free // (2**30), 1)

        uptime = sysinfo["uptime"]
        load = round(sysinfo["load"][1] / 65536.0, 2)
        memory_usage = 100 - round(100 * sysinfo["memory"]["available"] / sysinfo["memory"]["total"])

        with open("/sys/class/thermal/thermal_zone0/temp",'r') as f:
                temp = f.read()
        temp = round(int(temp) / 1000)
        #print(temp)


        try:
                #smartctl -x /dev/sda | grep "Current Temp"
                smartdata_output = subprocess.check_output(["smartctl", "-A", "-j", "/dev/sda"])
                smartdata = json.loads(smartdata_output)
                smart_temperature = smartdata["temperature"]["current"]

        except subprocess.CalledProcessError:
                print ("smartctl command failed...")

        monitor_dict["uptime"] = uptime
        monitor_dict["load"] = load
        monitor_dict["temperature"] = temp
        monitor_dict["memory_usage"] = memory_usage
        monitor_dict["disk_usage"] = disk_usage
        monitor_dict["disk_free"] = disk_free
        monitor_dict["disk_temperature"] = smart_temperature
        monitor_dict["movies"] = movies_usage
        monitor_dict["series"] = series_usage
        monitor_dict["lxc"] = lxc_list
        #print(monitor_dict)
        if debug == 0:
                try:
                    client.connect("xxx.xxx.xxx.xxx")
                    infot = client.publish(mqtt_topic, json.dumps(monitor_dict).encode('utf-8'), 0, True)
                    infot.wait_for_publish()
                    client.disconnect()
                except:
                    pass

        if debug == 0:
            time.sleep(300)
        else:
            print(monitor_dict)
            time.sleep(5)

