/*
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Tom Cubie <tangliang@allwinnertech.com>
 *
 * a simple tool to generate bootable image for sunxi platform.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

typedef unsigned char u8;
typedef unsigned int u32;

/* boot head definition from sun4i boot code */
typedef struct boot_file_head {
	u32 jump_instruction;	/* one intruction jumping to real code */
	u8 magic[8];		/* ="eGON.BT0" or "eGON.BT1", not C-style str */
	u32 check_sum;		/* generated by PC */
	u32 length;		/* generated by PC */
#if 1
	/* We use a simplified header, only filling in what is needed by the
	 * boot ROM. To be compatible with Allwinner tools the larger header
	 * below should be used, followed by a custom header if desired. */
	u8 pad[12];		/* align to 32 bytes */
#else
	u32 pub_head_size;	/* the size of boot_file_head_t */
	u8 pub_head_vsn[4];	/* the version of boot_file_head_t */
	u8 file_head_vsn[4];	/* the version of boot0_file_head_t or
				   boot1_file_head_t */
	u8 Boot_vsn[4];		/* Boot version */
	u8 eGON_vsn[4];		/* eGON version */
	u8 platform[8];		/* platform information */
#endif
} boot_file_head_t;

#define BOOT0_MAGIC                     "eGON.BT0"
#define STAMP_VALUE                     0x5F0A6C39

/* check sum functon from sun4i boot code */
int gen_check_sum(void *boot_buf)
{
	boot_file_head_t *head_p;
	u32 length;
	u32 *buf;
	u32 loop;
	u32 i;
	u32 sum;

	head_p = (boot_file_head_t *) boot_buf;
	length = head_p->length;
	if ((length & 0x3) != 0)	/* must 4-byte-aligned */
		return -1;
	buf = (u32 *) boot_buf;
	head_p->check_sum = STAMP_VALUE;	/* fill stamp */
	loop = length >> 2;

	/* calculate the sum */
	for (i = 0, sum = 0; i < loop; i++)
		sum += buf[i];

	/* write back check sum */
	head_p->check_sum = sum;

	return 0;
}

#define ALIGN(x, a) __ALIGN_MASK((x), (typeof(x))(a)-1)
#define __ALIGN_MASK(x, mask) (((x)+(mask))&~(mask))

#define SUN4I_SRAM_SIZE (24 * 1024)
#define SRAM_LOAD_MAX_SIZE (SUN4I_SRAM_SIZE - sizeof(boot_file_head_t))
#define BLOCK_SIZE 512

struct boot_img {
	boot_file_head_t header;
	char code[SRAM_LOAD_MAX_SIZE];
	char pad[BLOCK_SIZE];
};

int main(int argc, char *argv[])
{
	int fd_in, fd_out;
	struct boot_img img;
	unsigned file_size, load_size;
	int count;

	if (argc < 2) {
		printf
			("\tThis program makes an input bin file to sun4i bootable image.\n"
			 "\tUsage: %s input_file out_putfile\n", argv[0]);
		return EXIT_FAILURE;
	}

	fd_in = open(argv[1], O_RDONLY);
	if (fd_in < 0) {
		perror("Open input file:");
		return EXIT_FAILURE;
	}

	fd_out = open(argv[2], O_WRONLY | O_CREAT, 0666);
	if (fd_out < 0) {
		perror("Open output file:");
		return EXIT_FAILURE;
	}

	memset((void *)img.pad, 0, BLOCK_SIZE);

	/* get input file size */
	file_size = lseek(fd_in, 0, SEEK_END);
	printf("File size: 0x%x\n", file_size);

	if (file_size > SRAM_LOAD_MAX_SIZE)
		load_size = SRAM_LOAD_MAX_SIZE;
	else
		load_size = ALIGN(file_size, sizeof(int));
	printf("Load size: 0x%x\n", load_size);

	/* read file to buffer to calculate checksum */
	lseek(fd_in, 0, SEEK_SET);
	count = read(fd_in, img.code, load_size);
	printf("Read 0x%x bytes\n", count);

	/* fill the header */
	img.header.jump_instruction =	/* b instruction */
		0xEA000000 |	/* jump to the first instr after the header */
		((sizeof(boot_file_head_t) / sizeof(int) - 2)
		 & 0x00FFFFFF);
	memcpy(img.header.magic, BOOT0_MAGIC, 8);	/* no '0' termination */
	img.header.length =
		ALIGN(load_size + sizeof(boot_file_head_t), BLOCK_SIZE);
	gen_check_sum((void *)&img);

	count = write(fd_out, (void *)&img, img.header.length);
	printf("Write 0x%x bytes\n", count);

	close(fd_in);
	close(fd_out);

	return EXIT_SUCCESS;
}