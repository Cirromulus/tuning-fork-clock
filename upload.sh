#!/bin/bash
make -C build && sudo picotool load build/stimmgabeluhr.uf2 -f
