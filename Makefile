CC=gcc
CROSS=/home/rsousa/projects/openwrt/sdk-cpe.evm-nas-2_00/staging_dir/toolchain-arm_v7-a_gcc-4.5-linaro_glibc-2.14_eabi/bin/arm-openwrt-linux-
CFLAGS=-O2 -Wall

all: tcp_test tcp_test_target

tcp_test_target: tcp_test.c
	$(CROSS)$(CC) $(CFLAGS) -O2 -Wall tcp_test.c -o tcp_test_target

tcp_test: tcp_test.c
	$(CC) $(CFLAGS) -O2 -Wall tcp_test.c -o tcp_test

