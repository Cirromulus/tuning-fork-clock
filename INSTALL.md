```
sudo apt install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib
```

```
git submodule update --init --recursive
```

It helps to have an installed [`picotool`](https://github.com/raspberrypi/picotool).
If not, it will probably be installed by the SDK.

Or, just use the following commands:
```
mkdir build
cd build
cmake ..
make
cp *.uf2 /media/$(user)/RPI-RP2
```