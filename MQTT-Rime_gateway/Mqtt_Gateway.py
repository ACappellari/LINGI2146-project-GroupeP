import paho.mqtt.client as mqtt
import serial
import re

# HELPER FUNCTIONS AND GLOBAL VARIABLES
# -------------------------------------

last_opt = 0

# CONNACK callback
def connect_callback(client, userdata, flags, rc):
   if rc==0:
       client.connected_flag = True
       print("Gateway connected to broker\n")

# PUBLISH callback
def publish_callback(client, userdata, result):
    print("Data published\n")

# Callback upon broker message reception (number of connected clients)
def message_callback(client, userdata, message):
    printf("Number of clients received\n")
    if message.payload==1:  # If only one connected client (ourself): no more subscriber connected
        opt=0
        ser.write(0)
    else:
        opt=1
    if last_opt-opt==-1:    # At initialization or when the broker gets new subscriber after unconnected period
        print("Sensor nodes operating mode? (0: silenced, 1:send data periodically, 2:send data when change)\n")    # Ask for option
        option=sys.argv[1]
        ser.write(option)
    last_opt=opt

# Get topic and value from root-sensor data
def getData(line):
    if line[0]=='#':
        topic =  line[1:3]
        val = int(re.search(r'\d+', line).group())
    return [topic, val]

# CONNECTION TO SIMULATED GATEWAY NODE
# ------------------------------------
ser = serial.Serial()
ser.baudrate = 115200
ser.port = 6100
ser.open()

# CONNECTION TO BROKER
# --------------------
# Client initialization
gateway = mqtt.Client("gateway")
mqtt.Client.connected_flag = False

# Connect to broker
broker = "192.168.1.184"
gateway.connect(broker)
gateway.on_connect = connect_callback
gateway.on_publish = publish_callback
gateway.subscribe("$SYS/broker/clients/connected")

# Loop
gateway.loop_start()
while(gateway.connected_flag):
    line = ser.readline()
    [topic, value] = getData(line)
    if topic=="TMP":
        gateway.publish(topic,value)
    else if topic=="ACC":
        gateway.publish(topic,value)
    else:
        print("Unknown topic\n")

gateway.loop_stop()
gateway.disconnect()

ser.close()