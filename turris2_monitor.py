#!/usr/bin/env python
import paho.mqtt.client as mqtt
import time
import json
import ubus
import shutil
import subprocess
import os


mqtt_topic = "turris2/monitor"
mqtt_address = "192.168.xxx.xxx"
#mqtt_address = "mqtt.lan"

monitor_dict = {"uptime": 0, "load": 0, "temperature": 0, "memory_usage": 100, "uplink_speed": 0, "uplink_strength": 0, "disk_usage": 0, "disk_free": 0, "lxc": "none"}

# initialize MQTT connection
client = mqtt.Client("turris2_monitor")

time.sleep(60)
disconnected_counter = 0

while True:
	ubus.connect()
	sysinfo_json = ubus.call("system", "info", {})
	iwinfo_json = ubus.call("iwinfo", "assoclist", { "device":"phy1-sta0"})
#	print(iwinfo_json[0])
	lxc_list = ubus.call("lxc", "list", {})
	ubus.disconnect()

	sysinfo = sysinfo_json[0]
	try:
		iwinfo = iwinfo_json[0].get("results", {})[0]
	except:
		pass

	#print(sysinfo)
	#print(iwinfo)

	total, used, free = shutil.disk_usage("/")
	disk_usage = round(100 * (used / total))
	disk_free = round(free // (2**30), 1)

	uptime = sysinfo["uptime"]
	load = round(sysinfo["load"][1] / 65536.0, 2)
	memory_usage = 100 - round(100 * sysinfo["memory"]["available"] / sysinfo["memory"]["total"])

	try:
		uplink_strength = iwinfo.get("signal", "0")
		uplink_speed = round(iwinfo.get("tx" ,{}).get("rate",0) / 1000, 1)
	except:
		uplink_strength = 0
		uplink_speed = 0

	with open("/sys/class/thermal/thermal_zone0/temp",'r') as f:
		temp = f.read()
	temp = round(int(temp) / 1000)
	#print(temp)

	try:
		#smartctl -A -l ssd -j /dev/sda
		smartdata_output = subprocess.check_output(["smartctl", "-A", "-l", "ssd", "-j", "/dev/sda"])
		smartdata = json.loads(smartdata_output)
		smart_temperature = smartdata["temperature"]["current"]
		smart_wear = smartdata["ata_device_statistics"]["pages"][0]["table"][0]["value"]

	except subprocess.CalledProcessError:
		print ("smartctl command failed...")

	monitor_dict["uptime"] = uptime
	monitor_dict["load"] = load
	monitor_dict["temperature"] = temp
	monitor_dict["memory_usage"] = memory_usage
	monitor_dict["uplink_strength"] = uplink_strength
	monitor_dict["uplink_speed"] = uplink_speed
	monitor_dict["disk_usage"] = disk_usage
	monitor_dict["disk_free"] = disk_free
	monitor_dict["disk_temp"] = smart_temperature
	monitor_dict["disk_wear"] = smart_wear
	monitor_dict["lxc"] = lxc_list
	monitor_dict["usr_notify"] = True
	if os.path.exists('/var/user_notify'):
		if len(os.listdir('/var/user_notify')) == 0:
			monitor_dict["usr_notify"] = False
	else:
		monitor_dict["usr_notify"] = False
	#print(monitor_dict)

	mqtt_connected = 1
	try:
		client.connect(mqtt_address)
	except:
		mqtt_connected = 0
		disconnected_counter = disconnected_counter + 1
	if mqtt_connected:
		infot = client.publish(mqtt_topic, json.dumps(monitor_dict).encode('utf-8'), 0, True)
		infot.wait_for_publish()
		client.disconnect()
	else:
		if disconnected_counter > 2:
			subprocess.run(['/sbin/wifi', 'down', 'radio1'])
			time.sleep(10)
			subprocess.run(['/sbin/wifi', 'up', 'radio1'])
			disconnected_counter = 0

	time.sleep(300)
