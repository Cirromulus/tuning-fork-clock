```
sudo apt install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib
```

```
git submodule update --init --recursive
```

```
mkdir build
cd build
cmake ..
make
cp hello_world.uf2 /media/pp/RPI-RP2
```