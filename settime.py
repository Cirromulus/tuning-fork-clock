#!/usr/bin/python3

import serial
from datetime import datetime, timezone
from time import sleep

def getTimeString():
    local_timezone = datetime.now(timezone.utc).astimezone()
    ## Decided to not handle the timezone separately.
    ## the worst that will happen is that leap-seconds and DST are applied at the wrong time,
    ## which is OK for me.
    # local_timezone_offs_seconds = local_timezone.utcoffset().seconds

    # Currently ignoring the information when and how much DST to add.
    # Problem for later.

    # will fit well into uint64, and microseconds with an ugly ascii protocol is a joke
    timestamp_ms = round(local_timezone.timestamp()*1000)
    return f"{timestamp_ms}"



# TODO: Make parameter
devicePath = '/dev/ttyACM0'
stringcode = 'ascii'

device = serial.Serial(devicePath, timeout=1)  # don't care for baudrate, is USB currently
assert(device.is_open)

while True:
    s = getTimeString()
    print("-> ", s)
    device.write((s + '\n').encode('ascii'))
    ret = device.readline().decode('ascii')
    if ret:
        print ("<- ", ret)
        if ret == 'OK':
            break
