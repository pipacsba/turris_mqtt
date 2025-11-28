#!/usr/bin/env python
import os
import sys
from subprocess import Popen, PIPE, STDOUT
import subprocess
import paho.mqtt.client as mqtt
import time
import json

# define accepted MAC adresses
user1 = "XX:XX:XX:XX:XX:XX"
user2 = "YY:YY:YY:YY:YY:YY"
guest = ["XX:XX:XX:YY:YY:YY", "YY:YY:YY:YY:YY:XX"]

mqtt_server = "xy.xy.xy.xy"

# initialize MQTT connection
client = mqtt.Client("turris_monitor")

# get initial connections
wlan0 = subprocess.run(["iwinfo", "phy0-ap0", "assoc"], stdout=subprocess.PIPE)
wlan1 = subprocess.run(["iwinfo", "phy1-ap0", "assoc"], stdout = subprocess.PIPE)
wlan0_s = wlan0.stdout.decode('ascii').rstrip()
wlan1_s = wlan1.stdout.decode('ascii').rstrip()

wlan_s = wlan0_s + wlan1_s

# fill initial data
presence_dict = {"date" : "N/A", "user1" : "Away", "user2" : "Away", "Guest" : "Away"}
if user1 in wlan_s:
	presence_dict["user1"] = "Home"
if user2 in wlan_s:
	presence_dict["user2"] = "Home"
if any(word in wlan_s for word in guest):
	presence_dict["Guest"] = "Home"
presence_dict["date"] = time.asctime()

#print(presence_dict)

# send initial data
client.connect(mqtt_server)
infot = client.publish("turris/presence",json.dumps(presence_dict).encode('utf-8'), 0, True)
infot.wait_for_publish()

# open event line
p = Popen(["iw", "event"], stdout=PIPE, stderr=STDOUT, bufsize=1)

# handle events
with p.stdout:
	for line in iter(p.stdout.readline, b''):
		line_s = line.decode('ascii').upper()
		#print(line_s)

		# change 1 will mean that data needs to be sent
		change = 0
		if user2 in line_s:
			change = 1
			if "NEW" in line_s:
				presence_dict["user2"] = "Home"
			else:
				presence_dict["user2"] = "Away"
		elif user1 in line_s:
			change = 1
			if "NEW" in line_s:
				presence_dict["user1"] = "Home"
			else:
				presence_dict["user1"] = "Away"
		elif any(word in line_s for word in guest):
			change = 1
			if "NEW" in line_s:
				presence_dict["Guest"] = "Home"
			else:
				presence_dict["Guest"] = "Away"
			
		if change == 1:
			# print("Change ", presence_dict)
			presence_dict["date"] = time.asctime()
			client.connect(mqtt_server)
			infot = client.publish("turris/presence", json.dumps(presence_dict).encode('utf-8'), 0, True)
			infot.wait_for_publish()
			client.disconnect()
p.wait()
