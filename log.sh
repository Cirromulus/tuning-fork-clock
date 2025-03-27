#!/bin/bash

logname=forklog_$(date --iso-8601)

cat /dev/ttyACM* | grep . | gzip > ${logname}.csv.gz