import psutil
import subprocess
import paho.mqtt.client as mqtt
import time
import json
from os import curdir, sep, listdir, path

# Global variables (save data over switches / MQTT messages)
message_received = 0
cpu_percent = 0
payload = ""

# state_topic: "home/turris"
# command_topic: "home/turris/get"
# The callback for when the client receives a CONNACK response from the server.
def on_connect(this_client, user_data, flags, rc):
    # Subscribing in on_connect() means that if we lose the connection and
    # reconnect then subscriptions will be renewed.
    this_client.subscribe("home/turris/get")


# The callback for when a PUBLISH message is received from the server.
def on_message(this_client, user_data, msg):
    # print(msg.topic + " " + str(msg.payload))
    global message_received
    message_received = 1
    global payload
    payload = msg.payload.decode('ASCII')

client = mqtt.Client("read_turris")
client.on_connect = on_connect
client.on_message = on_message


client.connect("192.168.17.118")

client.loop_start()

while True:
    if message_received == 1:
        message_received = 0
        if payload == "state":
          result = subprocess.run("/usr/local/bin/read_turris/read_turris", stdout=subprocess.PIPE)
          result = result.stdout.decode('ascii').rstrip()
          cpu_times = psutil.cpu_times_percent()
          result = result.replace("}", ", \"cpu_percent\": \"" + str(100 - cpu_times.idle) + "\", \"cpu_iowait\": \"" + str(cpu_times.iowait) + "\", \"cpu_softirq\": \"" + str(cpu_times.softirq) + "\", \"cpu_steal\": \"" + str(cpu_times.steal) + "\"}")
          print(result)
          client.publish("home/turris", result, 0, True)
        elif payload == "minidlna_reset":
          subprocess.run("/usr/local/bin/read_turris/reset_minidlna_turris")
        elif payload == "hass_db":
          subprocess.Popen("/usr/local/bin/read_turris/hass_db")
        # get list of db
          filelist = []
          for file in listdir("/mnt/movies/hass"):
                if file.endswith(".db"):
                    size = path.getsize(path.join("/mnt/movies/hass", file)) >> 20
                    filelist.append(file + "," + str(size) + "MB")
          client.publish("home/hass_db",json.dumps(filelist).encode('utf-8'), 0, True)
    else:
        time.sleep(2)
#        cpu_percent = psutil.cpu_percent()
#        cpu_times = psutil.cpu_times_percent()
#        print(cpu_times)
