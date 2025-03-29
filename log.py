#!/usr/bin/python
import sqlite3
import serial
from datetime import datetime, timedelta


# TODO: Make parameter
devicePath = '/dev/ttyACM0'
stringcode = 'ascii'

device = serial.Serial(devicePath, timeout=1)  # don't care for baudrate, is USB currently
assert(device.is_open)

TABLE_NAME = 'logdata'
# would be best to be able to parse initial header... hm.
expectedFormat = [
    'Period [us]',
    'Derived Frequency [Hz]',
    'Temperature [0.01 DegCelsius]',
    'Pressure [2^(-8) Pa]',
    'Humidity [2^(-10) %RH]',
    # Rest is derived, don't care for now
]
db_file_name = datetime.today().strftime('%Y-%m-%d_%H-%M-%S') + "_sensor_log.db"
db = sqlite3.connect(db_file_name)
db_con = db.cursor()

# TODO: Types?
db_con.execute(f"CREATE TABLE {TABLE_NAME} ({', '.join(expectedFormat)})")
db.commit()

PRINT_EVERY = timedelta(seconds=5)
lastprint = datetime.fromtimestamp(0)

print (f"Writing into '{db_file_name}'")

try:
    while (device.is_open):
        line = device.readline().decode(stringcode).rstrip()
        # print (line)
        elements = line.split(',')
        if (len(elements) < len(expectedFormat)):
            print (f"{line} is not of the expected format")
            continue

        # for i, element in enumerate(expectedFormat):
        #     print (f"{element}: {elements[i]}")

        db_con.execute(f"INSERT INTO {TABLE_NAME} VALUES ({', '.join(elements[:len(expectedFormat)])})")

        if datetime.now() > lastprint + PRINT_EVERY:
            numrows = db_con.execute(f"SELECT COUNT(1) from {TABLE_NAME}").fetchone()[0]
            print (f"Currently collected {numrows} samples.", end='\r')
            lastprint = datetime.now()
except:
    print("\nExceptional stuff.")
    pass

print (f"Committing db as {db_file_name}...")
db.commit()
db.close()
print ("done.")

