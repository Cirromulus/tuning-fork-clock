#!/usr/bin/python
import sqlite3
import serial
from datetime import datetime, timedelta

import data


# TODO: Make parameter
devicePath = '/dev/ttyACM0'
stringcode = 'ascii'

device = serial.Serial(devicePath, timeout=1)  # don't care for baudrate, is USB currently
assert(device.is_open)

expectedCSVSerialFormat = [
    "Period [us]",
    "Temperature [0.01 DegC]",
    "Pressure [2^(-8) Pa]",
    "Humidity [2^(-10) %RH]",
    "Frequency [Hz]",
    "Temperature [DegC Rounded]",
    "Pressure [Pa Rounded]",
    "Humidity [%RH Rounded]",
]
# TODO: Check against header somehow

db_file_name = datetime.today().strftime('%Y-%m-%d_%H-%M-%S') + "_sensor_log.db"
db = sqlite3.connect(db_file_name)
db_con = db.cursor()

db_con.execute(f"CREATE TABLE {data.TABLE_NAME} ({', '.join([col.getSql() for col in data.TABLE_FORMAT.values()])})")
db.commit()

PRINT_EVERY = timedelta(seconds=2)
lastprint = datetime.fromtimestamp(0)

print (f"Writing into '{db_file_name}'")

try:
    while (device.is_open):
        line = device.readline().decode(stringcode).rstrip()
        # print (line)
        elements = line.split(',')
        if (len(elements) < len(expectedCSVSerialFormat)):
            print (f"{line} is not of the expected format")
            continue

        # for i, element in enumerate(expectedFormat):
        #     print (f"{element}: {elements[i]}")

        # TODO: Make sure that this the exact order of csv values -> database columns
        db_con.execute(f"INSERT INTO {data.TABLE_NAME} VALUES ({', '.join(elements[:len(data.TABLE_FORMAT)])})")

        if datetime.now() > lastprint + PRINT_EVERY:
            numrows = db_con.execute(f"SELECT COUNT(1) from {data.TABLE_NAME}").fetchone()[0]
            print (f"Currently collected {numrows} samples.", end='\r')
            lastprint = datetime.now()
except:
    print("\nExceptional stuff.")
    pass

print (f"Committing db as {db_file_name}...")
db.commit()
db.close()
print ("done.")

