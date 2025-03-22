#!/bin/bash
make -C build &&  picotool load build/stimmgabeluhr.uf2 -f
