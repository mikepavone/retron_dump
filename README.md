# retron_dump
Tool for using the Retron 5 as a ROM dumper

# Instructions
1. Root your Retron 5 and enable ADB access (extract in this repository can assist with making a new firmware image)
1. Install the android NDK and adb
1. Build dumpgen with `make dumpgen`. You may need to set NDKPATH to point at the location you installed the NDK
1. Extract the emulator APK from the Retron update image
1. Extract libretron.so from the APK
1. Extract the FPGA bitstream from libretron.so and save it in a file named retron.fpga (in my copy this is at offset 0x43448 and has a length of 54756 bytes and an md5 of 06f705e45fe5c41d241d29ecc6c18530)
1. `adb push dumpgen /sbin`
1. `adb push retron.fpga /mnt/sdcard`
1. Dump your cart with the dump script. `dump myrom.bin` for automatic size detection or `dump SIZE myrom.bin` to specify a specific dump size
