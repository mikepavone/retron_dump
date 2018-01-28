NDKPATH?=$(HOME)/android/ndk-16
ARMCC?=$(NDKPATH)/bin/arm-linux-androideabi-gcc --sysroot=/home/mike/android/ndk-16/sysroot

dumpgen : dumpgen.c
	$(ARMCC) -std=gnu99  -o dumpgen dumpgen.c

extract : extract.c
	$(CC) -std=gnu99  -o extract extract.c

