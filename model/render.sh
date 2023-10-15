#!/bin/bash

for part in "all" "base" "top" "bottom"
do
    openscad Tuningforkholder.scad -D show=\"${part}\" -o Tuningforkholder_$part.stl &
done
