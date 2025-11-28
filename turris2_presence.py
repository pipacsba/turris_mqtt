#!/usr/bin/env python
import os
import sys
from subprocess import Popen, PIPE, STDOUT
import subprocess
import paho.mqtt.client as mqtt
import time
import json

# define accepted MAC adresses
guszti = "XX:XX:XX:XX:XX:XX"
piro = "XX:XX:XX:XX:XX:XX"
guest = ["XX:XX:XX:XX:XX:XX", "XX:XX:XX:XX:XX:XX", "XX:XX:XX:XX:XX:XX"]

# initialize MQTT connection
client = mqtt.Client("turris2_monitor")
mqtt_address = "192.168.XX.XX"

time.sleep(90)

# get initial connections
wlan0 = subprocess.run(["iwinfo", "phy0-ap0", "assoc"], stdout=subprocess.PIPE)
wlan0_s = wlan0.stdout.decode('ascii').rstrip()

wlan_s = wlan0_s

#print(wlan_s)

# fill initial data
presence_dict = {"date" : "N/A", "Guszti" : "Away", "Piro" : "Away", "Guest" : "Away"}
if guszti in wlan_s:
	presence_dict["Guszti"] = "Home"
if piro in wlan_s:
	presence_dict["Piro"] = "Home"
if any(word in wlan_s for word in guest):
	presence_dict["Guest"] = "Home"
presence_dict["date"] = time.asctime()

#print(presence_dict)

# send initial data
try:
    client.connect(mqtt_address)
    infot = client.publish("turris2/presence",json.dumps(presence_dict).encode('utf-8'), 0, True)
    infot.wait_for_publish()
except:
    pass

# open event line
p = Popen(["iw", "event"], stdout=PIPE, stderr=STDOUT, bufsize=1)

# handle events
with p.stdout:
	for line in iter(p.stdout.readline, b''):
		line_s = line.decode('ascii').upper()
		# print(line_s)

		# change 1 will mean that data needs to be sent
		change = 0
		if piro in line_s:
			change = 1
			if "NEW" in line_s:
				presence_dict["Piro"] = "Home"
			else:
				presence_dict["Piro"] = "Away"
		elif guszti in line_s:
			change = 1
			if "NEW" in line_s:
				presence_dict["Guszti"] = "Home"
			else:
				presence_dict["Guszti"] = "Away"
		elif any(word in line_s for word in guest):
			change = 1
			if "NEW" in line_s:
				presence_dict["Guest"] = "Home"
			else:
				presence_dict["Guest"] = "Away"
			
		if change == 1:
			# print("Change ", presence_dict)
			presence_dict["date"] = time.asctime()
			try:
				client.connect(mqtt_address)
				infot = client.publish("turris2/presence", json.dumps(presence_dict).encode('utf-8'), 0, True)
				infot.wait_for_publish()
				client.disconnect()
			except:
				pass
p.wait()
