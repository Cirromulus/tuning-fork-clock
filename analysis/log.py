#!/bin/env python
import sqlite3
import serial
from datetime import datetime, timedelta

import data


# TODO: Make parameter
devicePath = '/dev/serial/by-id/usb-FTDI_FT232R_USB_UART_A50285BI-if00-port0'
stringcode = 'ascii'

device = serial.Serial(devicePath, baudrate=230400, timeout=1)
assert(device.is_open)

db_file_name = datetime.today().strftime('%Y-%m-%d_%H-%M-%S') + "_sensor_log.db"
db = sqlite3.connect(db_file_name)
db_con = db.cursor()

db_con.execute(f"CREATE TABLE {data.TABLE_NAME} ({', '.join([col.getSql() for col in data.TABLE_FORMAT.values()])})")
db.commit()

PRINT_EVERY = timedelta(seconds=2)
lastprint = datetime.fromtimestamp(0)

print (f"Writing into '{db_file_name}'")
first_estimate_diff = None
try:
    while (device.is_open):
        try:
            line = device.readline().decode(stringcode).rstrip()
        except UnicodeDecodeError as e:
            print (e, line)
            continue

        elements = line.split(',')

        if len(elements) == 0:
            print (f"Got nothing. Is there a OSC LOCK?")
            continue

        if len(elements) != len(data.TABLE_FORMAT):
            print (f"'{line}' is not of the expected format: Got {len(elements)} elements instead of {len(data.TABLE_FORMAT)}")
            continue

        if '[' in elements[0]:
            print (f"Probably encountered header. Ignoring.")
            continue

        # TODO: Make sure that this the exact order of csv values -> database columns
        db_con.execute(f"INSERT INTO {data.TABLE_NAME} VALUES ({', '.join(elements[:len(data.TABLE_FORMAT)])})")

        if datetime.now() > lastprint + PRINT_EVERY:
            numrows = db_con.execute(f"SELECT COUNT(1) from {data.TABLE_NAME}").fetchone()[0]
            print (f"\rCurrently collected {numrows} samples.", end=' ')
            currentReportedDrift = int(elements[-1]) # Todo: get named parameter and offset
            if not first_estimate_diff:
                first_estimate_diff = currentReportedDrift
            print (f" Current estimate diff: {currentReportedDrift} us ({first_estimate_diff - currentReportedDrift} since start of log)", end=' ')
            db.commit()  # Also, commit while we are at it
            lastprint = datetime.now()
except KeyboardInterrupt:
    print("Exceptional stuff")
    pass
except serial.SerialException:
    print ("Disco! (disco who?) Disconnected!")

print (f"Committing db as {db_file_name}...")
db.commit()
db.close()
print ("done.")

