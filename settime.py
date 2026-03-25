#!./venv/bin/python3

import serial
from datetime import datetime, timezone
import zoneinfo
from time import sleep
from pathlib import Path # TODO: Use normal file io?
import argparse

parser = argparse.ArgumentParser(
                    prog='settime',
                    description='send current time to the more precise tuning fork clock')


def getTimeString():
    local_timezone = datetime.now(timezone.utc).astimezone()
    # ms will fit well into uint64, and microseconds with an ugly ascii protocol is a joke
    timestamp_ms = round(local_timezone.timestamp()*1000)
    return f"{timestamp_ms}"

def getTzString():
    """
    The target system does not seem to have the complete zoneinfo.
    So put the full describing string:
    https://www.man7.org/linux/man-pages/man3/tzset.3.html

    >  Here is an example for New Zealand, where the standard time (NZST)
    >  is 12 hours ahead of UTC, and daylight saving time (NZDT), 13
    >  hours ahead of UTC, runs from September's last Sunday, at the
    >  default time 02:00:00, to April's first Sunday at 03:00:00.
    >       TZ="NZST-12:00:00NZDT-13:00:00,M9.5.0,M4.1.0/3"
    """

    def getTZStringFromLocation(loc):
        zones = zoneinfo.available_timezones()
        if loc not in zones:
             raise f"'{loc}' not in available timezones ({zones})"
        # path to IANA tz db on your system, adjust if needed
        basepath = Path("/usr/share/zoneinfo/")
        with open(basepath / loc, "rb") as fobj:
            content = fobj.readlines()
            return content[-1].decode("ASCII").strip("\n")

    return getTZStringFromLocation(datetime.now(timezone.utc).astimezone().tzinfo.tzname(None))

parser.add_argument('serial_port')
parser.add_argument('--baudrate', default=230400)
parser.add_argument('--stringcode', default='ascii')
args = parser.parse_args()

device = serial.Serial(args.serial_port, baudrate=args.baudrate, timeout=2)
assert(device.is_open)
assert(device.baudrate == args.baudrate)
print (f"Using device {device}")

while True:
    command = getTimeString() + ' ' + getTzString()
    print("--> ", command)

    # clear input buffer
    device.readline().decode(args.stringcode).strip()

    # send command
    device.write((command + '\n').encode(args.stringcode))

    # expect return
    ret = device.readline().decode(args.stringcode).strip()
    if ret:
        print ("<-- ", ret)
        if 'OK' in ret:
            returned_timestamp = ret.split(' ')[0]
            expected_timestamp = command.split(' ')[0]
            if returned_timestamp == expected_timestamp:
                break
            else:
                print ("incorrect timestamp return.")
                print (f"Expected: '{expected_timestamp}', got: '{returned_timestamp}'.")
                print ("Trying again")
