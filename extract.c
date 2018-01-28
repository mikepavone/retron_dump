/*
 Copyright 2018 Michael Pavone
 This file is part of retron_dump.
 retron_dump is free software distributed under the terms of the GNU General Public License version 3 or greater. See LICENSE for full license text.
*/
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define DIR_SIZE 0xE4
#define HEADER_SIZE 0x66

#define ENTRY_SIZE 0x39
uint8_t header[DIR_SIZE+HEADER_SIZE];
uint8_t buffer[0x800];


#define TOP_MAGIC "RKFW"
#define TOP_MAGIC_SIZE (sizeof(TOP_MAGIC)-1)
#define BOOT_MAGIC "BOOT"
#define BOOT_MAGIC_SIZE (sizeof(BOOT_MAGIC)-1)
#define SYSTEM_MAGIC "RKAF"
#define SYSTEM_MAGIC_SIZE (sizeof(SYSTEM_MAGIC)-1)
#define ANDROID_MAGIC "ANDROID!"
#define ANDROID_MAGIC_SIZE (sizeof(ANDROID_MAGIC)-1)

#define BOOT_OFF 0x19
#define SYSTEM_OFF 0x21
#define BOOT_FNAME_OFF 5
#define BOOT_FNAME_SIZE 0x28
#define BOOT_OFF_OFF (BOOT_FNAME_OFF+BOOT_FNAME_SIZE)
#define BOOT_SIZE_OFF (BOOT_OFF_OFF+4)
#define SYSTEM_DIR_OFF 0x88
#define SYSTEM_NAME_SIZE 0x20
#define SYSTEM_PATH_SIZE 0x3C
#define SYSTEM_OFF_OFF (SYSTEM_NAME_SIZE+SYSTEM_PATH_SIZE+sizeof(uint32_t))
#define SYSTEM_SIZE_OFF (SYSTEM_NAME_SIZE+SYSTEM_PATH_SIZE+4*sizeof(uint32_t))
#define SYSTEM_ENTRY_SIZE (SYSTEM_NAME_SIZE+SYSTEM_PATH_SIZE+5*sizeof(uint32_t))

#define BOOT_FILE_PREFIX "boot/"
#define SYSTEM_FILE_PREFIX "system/"

#define KERN_SIZE_OFF 0x8
#define RDISK_SIZE_OFF 0x10
#define PAGE_SIZE_OFF 0x24

uint32_t getu32le(uint8_t *off)
{
	return off[0] | off[1] << 8 | off[2] << 16 | off[3] << 24;
}

void checked_read(FILE *f, uint8_t *buffer, uint32_t size, char *fname)
{
	if (size != fread(buffer, 1, size, f)) {
		fprintf(stderr, "Failed to read from %s\n", fname);
		exit(1);
	}
}

void check_magic(uint8_t *buffer, char *magic, uint32_t magic_size)
{
	if (memcmp(magic, buffer, magic_size)) {
		fprintf(stderr, "Expected magic %s, but got %.*s instead\n", magic, magic_size, buffer);
		exit(1);
	}
}

char *copy_fixed(uint8_t *buffer, uint32_t max_size, uint32_t inc)
{
	uint32_t size = 0;
	for (uint32_t cur=0; cur < max_size; cur+=inc)
	{
		if (buffer[cur]) {
			size++;
		} else {
			break;
		}
	}
	char *outbuf = malloc(size+1);
	for (uint32_t in=0,out=0; out < size; in+=inc,out+=1)
	{
		outbuf[out] = buffer[in];
	}
	outbuf[size] = 0;
	return outbuf;
}

char * alloc_concat(char * first, char * second)
{
	int flen = strlen(first);
	int slen = strlen(second);
	char * ret = malloc(flen + slen + 1);
	memcpy(ret, first, flen);
	memcpy(ret+flen, second, slen+1);
	return ret;
}

void copy_data(FILE *imf, char *ifname, uint32_t offset, FILE *outf, uint32_t fsize)
{
	fseek(imf, offset, SEEK_SET);
	while (fsize) {
		uint32_t chunk_size = fsize < sizeof(buffer) ? fsize : sizeof(buffer);
		checked_read(imf, buffer, chunk_size, ifname);
		fwrite(buffer, 1, chunk_size, outf);
		fsize -= chunk_size;
	}
}

void extract_rkfw(FILE *imf, char *fname)
{
	uint32_t boot_start = getu32le(header+BOOT_OFF);
	uint32_t boot_size = getu32le(header+BOOT_OFF+sizeof(uint32_t));
	uint32_t system_start = getu32le(header+SYSTEM_OFF);
	uint32_t system_size = getu32le(header+SYSTEM_OFF+sizeof(uint32_t));
	
	printf("Boot image offset: %X, size: %u\n", boot_start, boot_size);
	printf("System image offset: %X, size: %u\n", system_start, system_size);
	
	if (boot_size) {
		fseek(imf, boot_start, SEEK_SET);
		checked_read(imf, header, HEADER_SIZE+DIR_SIZE, fname);
		check_magic(header, BOOT_MAGIC, BOOT_MAGIC_SIZE);
		
		puts("\nName                 Size        Offset");
		puts( "----------------------------------------");
		for (uint32_t cur = HEADER_SIZE; cur < HEADER_SIZE+DIR_SIZE; cur += ENTRY_SIZE)
		{
			char *fname = copy_fixed(header + cur + BOOT_FNAME_OFF, BOOT_FNAME_SIZE, 2);
			uint32_t foff = getu32le(header + cur + BOOT_OFF_OFF);
			uint32_t fsize = getu32le(header + cur + BOOT_SIZE_OFF);
			printf("%-20s %-11u %-8X\n", fname, fsize, foff);
			char *path = alloc_concat(BOOT_FILE_PREFIX, fname);
			free(fname);
			//TODO: Make directories if necessary
			FILE *outf = fopen(path, "wb");
			if (!outf) {
				fprintf(stderr, "Failed to open %s for writing\n", path);
				exit(1);
			}
			copy_data(imf, fname, foff+boot_start, outf, fsize);
			fclose(outf);
			free(path);
		}
	}
	
	fseek(imf, system_start, SEEK_SET);
	checked_read(imf, header, SYSTEM_DIR_OFF+sizeof(uint32_t), fname);
	check_magic(header, SYSTEM_MAGIC, SYSTEM_MAGIC_SIZE);
		  //01234567890123456789 012345678901234567890123456789
	puts("\nName                 Full Name                      Size        Offset");
	puts(  "----------------------------------------------------------------------");
	uint32_t fcount = getu32le(header + SYSTEM_DIR_OFF);
	uint8_t *dir = malloc(fcount * SYSTEM_ENTRY_SIZE);
	checked_read(imf, dir, fcount * SYSTEM_ENTRY_SIZE, fname);
	for (uint32_t i = 0, cur=0; i < fcount; i++,cur+=SYSTEM_ENTRY_SIZE)
	{
		char *base_name = copy_fixed(dir + cur, SYSTEM_NAME_SIZE, 1);
		char *full_name = copy_fixed(dir + cur + SYSTEM_NAME_SIZE, SYSTEM_PATH_SIZE, 1);
		uint32_t foff = getu32le(dir + cur + SYSTEM_OFF_OFF);
		uint32_t fsize = getu32le(dir + cur + SYSTEM_SIZE_OFF);
		printf("%-20.20s %-30.30s %-11u %-8X\n", base_name, full_name, fsize, foff);
		
		char *path = alloc_concat(SYSTEM_FILE_PREFIX, full_name);
		free(base_name);
		free(full_name);
		//TODO: Make directories if necessary
		FILE *outf = fopen(path, "wb");
		if (!outf) {
			fprintf(stderr, "Failed to open %s for writing\n", path);
			exit(1);
		}
		copy_data(imf, fname, foff+system_start, outf, fsize);
		fclose(outf);
		free(path);
	}
	free(dir);
}

void extract_android(FILE *imf, char *fname)
{
	uint32_t kern_size = getu32le(header + KERN_SIZE_OFF);
	uint32_t rdisk_size = getu32le(header + RDISK_SIZE_OFF);
	uint32_t page_size = getu32le(header + PAGE_SIZE_OFF);
	
	FILE *outf = fopen("kernel", "wb");
	if (!outf) {
		fputs("Failed to open kernel for writing\n", stderr);
		exit(1);
	}
	copy_data(imf, fname, page_size, outf, kern_size);
	fclose(outf);
	uint32_t rdisk_off = ((kern_size + page_size - 1)/page_size + 1) * page_size;
	outf = fopen("ramdisk.gz", "wb");
	if (!outf) {
		fputs("Failed to open ramdisk.gz for writing\n", stderr);
		exit(1);
	}
	copy_data(imf, fname, rdisk_off, outf, rdisk_size);
	fclose(outf);
}

int main(int argc, char ** argv)
{
	if (argc < 2) {
		fputs("usage: extract IMAGE\n", stderr);
		exit(1);
	}
	FILE *imf = fopen(argv[1], "rb");
	if (!imf) {
		fprintf(stderr, "Failed to open %s\n", argv[1]);
		exit(1);
	}
	checked_read(imf, header, HEADER_SIZE, argv[1]);
	if (!memcmp(header, TOP_MAGIC, TOP_MAGIC_SIZE)) {
		extract_rkfw(imf, argv[1]);
	} else if(!memcmp(header, ANDROID_MAGIC, ANDROID_MAGIC_SIZE)) {
		extract_android(imf, argv[1]);
	} else {
		fprintf(stderr, "Unrecognized magic %.*s\n", (int)TOP_MAGIC_SIZE, header);
		exit(1);
	}
	

	return 0;
}
