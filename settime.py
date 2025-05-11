#!/usr/bin/env python3

import serial
from datetime import datetime, timezone
from time import sleep

def getTimeString():
    local_timezone = datetime.now(timezone.utc).astimezone()
    # ms will fit well into uint64, and microseconds with an ugly ascii protocol is a joke
    timestamp_ms = round(local_timezone.timestamp()*1000)
    return f"{timestamp_ms}"

def getTzString():
    # TODO: Generate that string from host locale
    """
    So for CET-1CEST

    The standard timezone is CET (Central European Time)
    The offset from UTC is -1
    The DST timezone is CEST (Central European Summer Time)
    """
    return "CET-1CEST"

# TODO: Make parameter
devicePath = '/dev/ttyACM0'
stringcode = 'ascii'

device = serial.Serial(devicePath, timeout=1)  # don't care for baudrate, is USB currently
assert(device.is_open)

while True:
    command = getTimeString() + ' ' + getTzString()
    print("--> ", command)

    device.write((command + '\n').encode('ascii'))
    ret = device.readline().decode('ascii').strip()
    if ret:
        print ("<-- ", ret)
        if 'OK' in ret:
            # TODO: Check returned timestamp for equivalence
            break
