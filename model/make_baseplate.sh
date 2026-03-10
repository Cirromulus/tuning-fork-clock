
#!/bin/bash

openscad Baseplate_and_holder.scad -D show="\"normal\"" -o Baseplate_and_holder_normal.stl &
a=$!
openscad Baseplate_and_holder.scad -D show="\"gummi\"" -o Baseplate_and_holder_tpu.stl &
b=$!

wait $a $b
echo "done"
