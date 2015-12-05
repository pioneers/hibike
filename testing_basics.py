import serial
import hibike_message as hm

servo = serial.Serial(/dev//ttyACM0, 115200)
time.sleep(1)

print "Starting Latency Tests:"
start = time.time()
hm.send(servo, hm.make_device_update(1, 20))
while hw.read(servo) in (None, 1):
    pass
end = time.time()

print [end-start]
