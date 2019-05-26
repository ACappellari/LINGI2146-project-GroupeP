import paho.mqtt.client as mqtt
import serial
import re
import time

# HELPER FUNCTIONS AND GLOBAL VARIABLES
# -------------------------------------

last_opt = 0

# CONNACK callback
def on_connect(client, userdata, flags, rc):
   if rc==0:
       client.connected_flag = True
       print("Gateway connected to broker\n")
   else:
	print("Bad connection return code=",rc)

# PUBLISH callback
def on_publish(client, userdata, result):
    print("Data published\n")

# Callback upon broker message reception (number of connected clients)
def on_message(client, userdata, message):
    time.sleep(1)
    print("Number of clients received\n")
    if message.payload==1:  # If only one connected client (ourself): no more subscriber connected
        opt=0
        ser.write(0)
    else:
        opt=1
    if last_opt-opt==-1:    # At initialization or when the broker gets new subscriber after unconnected period
        option=raw_input("Sensor nodes operating mode? (0: silenced, 1:send data periodically, 2:send data when change)\n")    # Ask for option
        ser.write(option)
    last_opt=opt

# Get topic and value from root-sensor data
def getData(line):
    if line[0]=='#':
	print("Read line: \n")
	print(line)
        topic =  line[1:4]
        val = int(re.search(r'\d+', line).group())
    return [topic, val]

# CONNECTION TO SIMULATED GATEWAY NODE
# ------------------------------------
ser = serial.Serial()
ser.baudrate = 115200
ser.port = '/dev/pts/29'
ser.open()

# CONNECTION TO BROKER
# --------------------
# Client initialization
mqtt.Client.connected_flag = False
gateway = mqtt.Client("gateway",protocol=mqtt.MQTTv31)

# Callbacks
gateway.on_connect = on_connect
gateway.on_publish = on_publish
gateway.on_message = on_message

# Subscribe to number of connected clients

# Loop
# Connect to broker
gateway.connect("127.0.0.1",1884)
gateway.subscribe("$SYS/broker/subscriptions/count")

gateway.loop_start()

while not gateway.connected_flag:
	print("In wait loop\n")
	time.sleep(1)
print("In main loop\n")

while(gateway.connected_flag):
    line = ser.readline()
    [topic, value] = getData(line)
    print(topic)
    print(value)
    if topic=="TMP":
        gateway.publish(topic,value)
	print("Published data: [topic, value] = \n")
	print(topic)
	print(value)
    elif topic=="ACC":
        gateway.publish(topic,value)
	print("Published data: [topic, value] = \n")
	print(topic)
	print(value)
    else:
        print("Unknown topic\n")

print("Out of while")
gateway.loop_stop()
gateway.disconnect()

ser.close()