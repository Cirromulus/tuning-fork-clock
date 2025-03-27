#!/bin/bash

logname=forklog_$(date --iso-8601)

echo "Unfortunately, CTRL+C will kill zipping. Disconnect wire instead."
cat /dev/ttyACM* | grep . | gzip > ${logname}.csv.gz