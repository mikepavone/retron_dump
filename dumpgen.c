/*
 Copyright 2016 Michael Pavone
 This file is part of retron_dump.
 retron_dump is free software distributed under the terms of the GNU General Public License version 3 or greater. See LICENSE for full license text.
*/
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#define IOCTL_IDENT 'G'
#define IOCTL_GPIO_SET_BITS 		_IOWR(IOCTL_IDENT, 0, int)
#define IOCTL_GPIO_GET_BITS 		_IOWR(IOCTL_IDENT, 1, int)
#define IOCTL_GPIO_SET_DIRECTION 	_IOWR(IOCTL_IDENT, 2, int)
#define IOCTL_GPIO_SET_PULL	 		_IOWR(IOCTL_IDENT, 3, int)
#define IOCTL_GPIO_ACCESS_CTRL		_IOWR(IOCTL_IDENT, 4, int)
#define IOCTL_SET_LEDS				_IOWR(IOCTL_IDENT, 6, int)
#define IOCTL_GPIO_PORT_MUTEX_OP	_IOWR(IOCTL_IDENT, 11, int)
#define IOCTL_GPIO_PORT_MUTEX_RESET _IOWR(IOCTL_IDENT, 12, int)
#define IOCTL_DRIVER_VERSION 		_IOWR(IOCTL_IDENT, 13, int)
#define IOCTL_PCBA_VERSION 			_IOWR(IOCTL_IDENT, 14, int)

#define GPIO_PORT_FPGA			0x00
#define	GPIO_PORT_JOY0			0x01
#define	GPIO_PORT_JOY1			0x02

#define GPIO_ACCESS_CTRL_OFF		0
#define GPIO_ACCESS_CTRL_ON			1
#define GPIO_ACCESS_CTRL_LOCKED		2

#define RETRON_MUTEX_UNLOCK			0
#define RETRON_MUTEX_LOCK			1

#define DATA_BUS_MASK 0xFF

#define CPU_DOUT_BUSY 0x100
#define CPU_INIT_B    0x200
#define CPU_CSI_B     0x400
#define CPU_PROG_B    0x800
#define CPU_DONE      0x1000
#define CPU_CCLK      0x2000
#define CPU_RDRW      0x8000

#define DELAY 50


void enable_gpio(int fd)
{
	if (ioctl(fd, IOCTL_GPIO_ACCESS_CTRL, GPIO_ACCESS_CTRL_ON) < 0) {
		fputs("Failed to enable GPIO access\n", stderr);
		exit(1);
	}
}

void disable_gpio(int fd)
{
	if (ioctl(fd, IOCTL_GPIO_ACCESS_CTRL, GPIO_ACCESS_CTRL_OFF) < 0) {
		fputs("Failed to disable GPIO access\n", stderr);
		exit(1);
	}
}


void lock_port(int fd, int port)
{
	int args[] = {port, RETRON_MUTEX_LOCK};
	if (ioctl(fd, IOCTL_GPIO_PORT_MUTEX_OP, (int)args) < 0) {
		disable_gpio(fd);
		fputs("Failed to lock port\n", stderr);
		exit(1);
	}
}

void unlock_port(int fd, int port)
{
	int args[] = {port, RETRON_MUTEX_UNLOCK};
	if (ioctl(fd, IOCTL_GPIO_PORT_MUTEX_OP, (int)args) < 0) {
		disable_gpio(fd);
		fputs("Failed to unlock port\n", stderr);
		exit(1);
	}
}

void set_gpio_dir(int fd, int port, int mask, int dir)
{
	int args[] = {port, mask, dir};
	if (ioctl(fd, IOCTL_GPIO_SET_DIRECTION, (int)args) < 0) {
		fputs("Failed to set GPIO direction\n", stderr);
		unlock_port(fd, GPIO_PORT_FPGA);
		exit(1);
	}
}

#define DIR_READ 1
#define DIR_WRITE 2
int dir;
void set_dir_read(int fd)
{
	if (dir != DIR_READ) {
		set_gpio_dir(fd, GPIO_PORT_FPGA, 0xFF, 0);
		dir = DIR_READ;
	}
}

void set_dir_write(int fd)
{
	if (dir != DIR_WRITE) {
		set_gpio_dir(fd, GPIO_PORT_FPGA, 0xFF, 0xFF);
		dir = DIR_WRITE;
	}
}

void set_bits(int fd, int port, int mask, int value)
{
	int args[] = {port, mask, value};
	if (ioctl(fd, IOCTL_GPIO_SET_BITS, (int)args) < 0) {
		fputs("Failed to set GPIO bits\n", stderr);
		unlock_port(fd, GPIO_PORT_FPGA);
		exit(1);
	}
}

int get_bits(int fd, int port, int mask)
{
	int args[] = {port, mask};
	if (ioctl(fd, IOCTL_GPIO_GET_BITS, (int)args) < 0) {
		fputs("Failed to get GPIO bits\n", stderr);
		unlock_port(fd, GPIO_PORT_FPGA);
		exit(1);
	}
	return args[0];
}

void clear_busy(int fd)
{
	set_bits(fd, GPIO_PORT_FPGA, CPU_DOUT_BUSY, 0);
}

void set_busy(int fd)
{
	set_bits(fd, GPIO_PORT_FPGA, CPU_DOUT_BUSY, CPU_DOUT_BUSY);
}

uint8_t reverse_bits(uint8_t val)
{
	val = val << 4 | val >> 4;
	val = (val & 0x33) << 2 | (val & 0xCC) >> 2;
	return (val & 0x55) << 1 | (val & 0xAA) >> 1;
}

void write_byte(int fd, int val)
{
	set_dir_write(fd);
	set_bits(fd, GPIO_PORT_FPGA, DATA_BUS_MASK, val);
	clear_busy(fd);
	usleep(DELAY);
	set_busy(fd);
	usleep(DELAY);
	//printf("wrote: %X\n", val & DATA_BUS_MASK);
}

void write_config_byte(int fd, uint8_t val)
{
	set_bits(fd, GPIO_PORT_FPGA, CPU_CCLK, 0);
	set_bits(fd, GPIO_PORT_FPGA, DATA_BUS_MASK, reverse_bits(val));
	set_bits(fd, GPIO_PORT_FPGA, CPU_CCLK, CPU_CCLK);
}

int wait_low(int fd, int bits, int max)
{
	int count;
	for (count = 0; count < max; count++)
	{
		if (!get_bits(fd, GPIO_PORT_FPGA, bits)) {
			break;
		}
	}
	return count;
}

int wait_high(int fd, int bits, int max)
{
	int count;
	for (count = 0; count < max; count++)
	{
		if (get_bits(fd, GPIO_PORT_FPGA, bits)) {
			break;
		}
	}
	return count;
}

void reset_fpga(int fd)
{
	printf("State before reset: %X\n", get_bits(fd, GPIO_PORT_FPGA, CPU_INIT_B | CPU_DONE));
	set_bits(fd, GPIO_PORT_FPGA, CPU_PROG_B, 0);
	if (wait_low(fd, CPU_INIT_B, 100) == 100) {
		fputs("Failed to reset FPGA", stderr);
		unlock_port(fd, GPIO_PORT_FPGA);
		exit(1);
	}
	printf("State middle of reset: %X\n", get_bits(fd, GPIO_PORT_FPGA, CPU_INIT_B | CPU_DONE));
	set_bits(fd, GPIO_PORT_FPGA, CPU_PROG_B, CPU_PROG_B);
	if (wait_high(fd, CPU_INIT_B, 1000000) == 1000000) {
		fputs("Failed to reset FPGA", stderr);
		unlock_port(fd, GPIO_PORT_FPGA);
		exit(1);
	}
	printf("State after reset: %X\n", get_bits(fd, GPIO_PORT_FPGA, CPU_INIT_B | CPU_DONE));
}

void load_config(int fd, char *bitstream_path)
{
	FILE *f = fopen(bitstream_path, "rb");
	if (!f) {
		fprintf(stderr, "Could not open FPGA bitstream from %s\n", bitstream_path);
		unlock_port(fd, GPIO_PORT_FPGA);
		exit(1);
	}
	fseek(f, 0, SEEK_END);
	long fsize = ftell(f);
	rewind(f);
	char * bits = malloc(fsize);
	if (fread(bits, 1, fsize, f) != fsize) {
		fputs("Error reading from bitstream file", stderr);
		unlock_port(fd, GPIO_PORT_FPGA);
		exit(1);
	}
	fclose(f);
	//set initial pin state
	int outputs = DATA_BUS_MASK | CPU_RDRW | CPU_CCLK | CPU_PROG_B | CPU_CSI_B;
	set_gpio_dir(fd, GPIO_PORT_FPGA, 0xFAFF, outputs);
	set_bits(fd, GPIO_PORT_FPGA, 0xe8ff, 0xe8ff);
	//set_bits(fd, GPIO_PORT_FPGA, outputs, outputs);
	
	reset_fpga(fd);
	set_bits(fd, GPIO_PORT_FPGA, CPU_RDRW, 0);
	set_bits(fd, GPIO_PORT_FPGA, CPU_CSI_B, 0);
	for (long i = 0; i < fsize; i++)
	{
		write_config_byte(fd, bits[i]);
	}
	free(bits);
	//wait for done
	int i;
	for (i = 0; i < 100; i++) {
		set_bits(fd, GPIO_PORT_FPGA, CPU_CCLK, 0);
		int status = get_bits(fd, GPIO_PORT_FPGA, CPU_DONE | CPU_INIT_B);
		if (status & CPU_DONE) {
			break;
		}
		if (!(status & CPU_INIT_B)) {
			fputs("FPGA signaled CRC error\n", stderr);
			unlock_port(fd, GPIO_PORT_FPGA);
			exit(1);
		}
		set_bits(fd, GPIO_PORT_FPGA, CPU_CCLK, CPU_CCLK);
	}
	if (i == 100) {
		fputs("FPGA failed to set DONE bit\n", stderr);
		unlock_port(fd, GPIO_PORT_FPGA);
		exit(1);
	}
	printf("State after DONE: %X\n", get_bits(fd, GPIO_PORT_FPGA, CPU_INIT_B | CPU_DONE));
	for (i = 0; i < 100; i++) {
		set_bits(fd, GPIO_PORT_FPGA, CPU_CCLK, 0);
		set_bits(fd, GPIO_PORT_FPGA, CPU_CCLK, CPU_CCLK);
	}
	printf("State after end config: %X\n", get_bits(fd, GPIO_PORT_FPGA, CPU_INIT_B | CPU_DONE));
	set_bits(fd, GPIO_PORT_FPGA, CPU_RDRW | CPU_CSI_B, CPU_RDRW | CPU_CSI_B);
}

void write_u32le(int fd, uint32_t val)
{
	write_byte(fd, val);
	write_byte(fd, val >> 8);
	write_byte(fd, val >> 16);
	write_byte(fd, val >> 24);
}

void cart_on(int fd)
{
	write_byte(fd, 0x27);
}

void cart_off(int fd)
{
	write_byte(fd, 0x26);
}

void setup_md(int fd)
{
	write_byte(fd, 1);
	write_byte(fd, 0xB);
	write_u32le(fd, 2);
}

void write_magic(int fd, int flag)
{
	write_byte(fd, 0x24);
	write_byte(fd, 0xB);
	write_u32le(fd, 2);
	write_byte(fd, 8);
	write_u32le(fd, 0x5555);
	write_byte(fd, 31);
	write_byte(fd, 0xAA);
	write_byte(fd, 8);
	write_u32le(fd, 0x2AAA);
	write_byte(fd, 31);
	write_byte(fd, 0x55);
	write_byte(fd, 8);
	write_u32le(fd, 0x5555);
	write_byte(fd, 31);
	write_byte(fd, 0xB0);
	write_byte(fd, 8);
	write_u32le(fd, 0);
	write_byte(fd, 31);
	write_byte(fd, flag ? 1 : 0);
}

uint8_t read_byte(int fd)
{
	set_dir_read(fd);
	clear_busy(fd);
	usleep(DELAY);
	int i = 0;
	if (1000 == wait_low(fd, CPU_INIT_B, 1000)) {
		fputs("timed out wiating for data (low)\n", stderr);
		unlock_port(fd, GPIO_PORT_FPGA);
		exit(1);
	}
	uint8_t ret = get_bits(fd, GPIO_PORT_FPGA, 0xFF);
	set_busy(fd);
	if (1000 == wait_high(fd, CPU_INIT_B, 1000)) {
		fputs("timed out wiating for data (high)\n", stderr);
		unlock_port(fd, GPIO_PORT_FPGA);
		exit(1);
	}
	return ret;
}

uint16_t read_u16le(int fd)
{
	uint8_t lsb = read_byte(fd);
	return lsb | read_byte(fd) << 8;
}

uint16_t cart_status(int fd)
{
	write_byte(fd, 4);
	write_byte(fd, 0xE);
	return read_u16le(fd);
}

void read_range(int fd, uint8_t *dst, uint32_t start, uint32_t len)
{
	write_byte(fd, 8);
	write_u32le(fd, start);
	write_byte(fd, 0xC);
	write_u32le(fd, len-1);
	write_byte(fd, 0x10);
	for (; len > 0; len--, dst++)
	{
		*dst = read_byte(fd);
	}
}

void read_range_swapped(int fd, uint8_t *dst, uint32_t start, uint32_t len)
{
	write_byte(fd, 8);
	write_u32le(fd, start/2);
	write_byte(fd, 0xC);
	write_u32le(fd, len-1);
	write_byte(fd, 0x10);
	for (; len > 0; len-=2, dst+=2)
	{
		dst[1] = read_byte(fd);
		*dst = read_byte(fd);
	}
}

void do_verify_setup(int fd)
{
	set_dir_read(fd);
	set_bits(fd, GPIO_PORT_FPGA, CPU_CSI_B, CPU_CSI_B);
	set_busy(fd);
	usleep(DELAY);
	set_bits(fd, GPIO_PORT_FPGA, CPU_CSI_B, 0);
	usleep(DELAY);
	set_bits(fd, GPIO_PORT_FPGA, CPU_CSI_B, CPU_CSI_B);
	usleep(DELAY);
}

void verify_fpga(int fd)
{
	uint8_t buf[7];
	do_verify_setup(fd);
	write_byte(fd, 0xF);
	set_dir_read(fd);
	int i;
	for (i = 0; i < 7; i++) {
		buf[i] = read_byte(fd);
		printf("%d: %X\n", i, buf[i]);
	}
	for (i = 1; i < 7; i++) {
		if (buf[i] != buf[0]) {
			break;
		}
	}
	if (i == 7) {
		fputs("All verification bytes are the same\n", stderr);
		//unlock_port(fd, GPIO_PORT_FPGA);
		//exit(1);
	}
	for (i = 0; i < 3; i++)
	{
		do_verify_setup(fd);
		write_byte(fd, 0xF);
		set_dir_read(fd);
		for (int j = 0; j < 7; j ++)
		{
			uint8_t byte = read_byte(fd);
			if (buf[j] != byte) {
				fprintf(stderr, "Verification mismatch: Expected %X, but got %X", buf[j], byte);
				//unlock_port(fd, GPIO_PORT_FPGA);
				//exit(1);
			}
			/*
			clear_busy(fd);
			if (wait_low(fd, CPU_INIT_B, 5) == 5) {
				fputs("Timeout wiating for ready bit\n", stderr);
			}
			uint8_t byte = get_bits(fd, GPIO_PORT_FPGA, DATA_BUS_MASK);
			if (buf[j] != byte) {
				fprintf(stderr, "Verification mismatch: Expected %X, but got %X", buf[j], byte);
				//unlock_port(fd, GPIO_PORT_FPGA);
				//exit(1);
			}
			usleep(DELAY);
			set_busy(fd);
			usleep(DELAY);*/
		}
	}
	do_verify_setup(fd);
}

void set_leds(int fd, int value)
{
	write_byte(fd, 0x25);
	write_byte(fd, 0x1F);
	write_byte(fd, value);
	set_dir_read(fd);
}

#define SSF2 "SUPER STREET FIGHTER2"

uint8_t buffer[0x800];

int main(int argc, char ** argv)
{
	if (argc < 2) {
		fputs("Usage: dumpgen FILE\n", stderr);
		exit(1);
	}
	int retron = open("/dev/retron5", O_RDWR | O_SYNC);
	if (retron < 0) {
		fputs("Failed to open /dev/retron5\n", stderr);
		exit(1);
	}
	enable_gpio(retron);
	int outfd = -1;
	int force_size = -1;
	int do_led = 0;
	if (strcmp(argv[1], "-s")) {
		if (!strcmp(argv[1], "-l")) {
			do_led = 1;
		} else {
			char *fname;
			if (!strcmp(argv[1], "-f")) {
				if (argc < 4) {
					fputs("-f must be followed by size and destination filename\n", stderr);
					exit(1);
				}
				force_size = atoi(argv[2]);
				fname = argv[3];
			} else {
				fname = argv[1];
			}
			
			outfd = open(fname, O_WRONLY | O_TRUNC | O_CREAT, 0664);
			if (outfd < 0) {
				close(retron);
				fprintf(stderr, "Failed to open %s for writing\n", argv[1]);
				exit(1);
			}
		}
	}
	/*
	printf("SET_BITS: %X, GET_BITS: %X\n", IOCTL_GPIO_SET_BITS, IOCTL_GPIO_GET_BITS);
	printf("SET_DIRECTION: %X, SET_PULL: %X\n", IOCTL_GPIO_SET_DIRECTION, IOCTL_GPIO_SET_PULL);
	printf("ACCESS_CTRL: %X, SET_LEDS: %X\n", IOCTL_GPIO_ACCESS_CTRL, IOCTL_SET_LEDS);
	printf("PORT_MUTEX_OP: %X, PORT_MUTEX_RESET: %X\n", IOCTL_GPIO_PORT_MUTEX_OP, IOCTL_GPIO_PORT_MUTEX_RESET);
	printf("DRIVER_VERSION: %X, PCBA_VERSION: %X\n", IOCTL_DRIVER_VERSION, IOCTL_PCBA_VERSION);*/
	puts("locking FPGA port");
	lock_port(retron, GPIO_PORT_FPGA);
		puts("Loading FPGA bitstream");
		load_config(retron, "/mnt/sdcard/retron.fpga");
	
		puts("Setting pin direction");
		set_bits(retron, GPIO_PORT_FPGA, 0xFAFF, 0XFAFF);
		set_gpio_dir(retron, GPIO_PORT_FPGA, CPU_DOUT_BUSY | CPU_INIT_B, CPU_DOUT_BUSY);
		verify_fpga(retron);
		
		
		puts("Cart power on");
		cart_on(retron);
		
		printf("Cart status: %X\n", cart_status(retron));
		
		if (outfd >= 0) {
			puts("Setting up for MD reads");
			setup_md(retron);
			puts("dumping cartridge");
			read_range_swapped(retron, buffer, 0, sizeof(buffer));
			write(outfd, buffer, sizeof(buffer));
			uint32_t length = (buffer[0x1a4] << 24 | buffer[0x1a5] << 16 | buffer[0x1a6] << 8 | buffer[0x1a7]) + 1;
			if (length == 4*1024*1024  && !memcmp(buffer+0x120, SSF2, strlen(SSF2))) {
				length += 1024*1024;
			} else if (length > 4*1024*1024 && force_size < 0) {
				force_size = 4*1024*1024;
			}
			if (force_size >= 0) {
				fprintf(stderr, "Size of %d bytes read from header, forcing %d\n", length, force_size);
				length = force_size;
			}
			printf("Cartridge size is %X\n", length);
			for (uint32_t address = 0x800; address < length; address+=sizeof(buffer))
			{
				printf("\r%d%%", 100 * address / length);
				fflush(stdout);
				uint32_t size = sizeof(buffer) < length-address ? sizeof(buffer) : length-address;
				read_range_swapped(retron, buffer, address, size);
				write(outfd, buffer, sizeof(buffer));
			}
			puts("\nDONE");
		} else if (do_led) {
			set_leds(retron, strtol(argv[2], NULL, 16));
		}
		
		cart_off(retron);
	unlock_port(retron, GPIO_PORT_FPGA);
	close(retron);
	if (outfd >= 0) {
		close(outfd);
	}
	
	return 0;
}
