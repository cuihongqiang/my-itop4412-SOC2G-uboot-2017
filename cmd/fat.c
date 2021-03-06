/*
 * (C) Copyright 2002
 * Richard Jones, rjones@nexus-tech.net
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

/*
 * Boot support
 */
#include <common.h>
#include <command.h>
#include <s_record.h>
#include <net.h>
#include <ata.h>
#include <asm/io.h>
#include <mapmem.h>
#include <part.h>
#include <fat.h>
#include <fs.h>



int do_fat_size(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	return do_size(cmdtp, flag, argc, argv, FS_TYPE_FAT);
}

U_BOOT_CMD(
	fatsize,	4,	0,	do_fat_size,
	"determine a file's size",
	"<interface> <dev[:part]> <filename>\n"
	"    - Find file 'filename' from 'dev' on 'interface'\n"
	"      and determine its size."
);

int do_fat_fsload (cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	return do_load(cmdtp, flag, argc, argv, FS_TYPE_FAT);
}


U_BOOT_CMD(
	fatload,	7,	0,	do_fat_fsload,
	"load binary file from a dos filesystem",
	"<interface> [<dev[:part]> [<addr> [<filename> [bytes [pos]]]]]\n"
	"    - Load binary file 'filename' from 'dev' on 'interface'\n"
	"      to address 'addr' from dos filesystem.\n"
	"      'pos' gives the file position to start loading from.\n"
	"      If 'pos' is omitted, 0 is used. 'pos' requires 'bytes'.\n"
	"      'bytes' gives the size to load. If 'bytes' is 0 or omitted,\n"
	"      the load stops on end of file.\n"
	"      If either 'pos' or 'bytes' are not aligned to\n"
	"      ARCH_DMA_MINALIGN then a misaligned buffer warning will\n"
	"      be printed and performance will suffer for the load."
);

static int do_fat_ls(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	return do_ls(cmdtp, flag, argc, argv, FS_TYPE_FAT);
}

U_BOOT_CMD(
	fatls,	4,	1,	do_fat_ls,
	"list files in a directory (default /)",
	"<interface> [<dev[:part]>] [directory]\n"
	"    - list files from 'dev' on 'interface' in a 'directory'"
);

static int do_fat_fsinfo(cmd_tbl_t *cmdtp, int flag, int argc,
			 char * const argv[])
{
	int dev, part;
	struct blk_desc *dev_desc;
	disk_partition_t info;

	if (argc < 2) {
		printf("usage: fatinfo <interface> [<dev[:part]>]\n");
		return 0;
	}

	part = blk_get_device_part_str(argv[1], argv[2], &dev_desc, &info, 1);
	if (part < 0)
		return 1;

	dev = dev_desc->devnum;
	if (fat_set_blk_dev(dev_desc, &info) != 0) {
		printf("\n** Unable to use %s %d:%d for fatinfo **\n",
			argv[1], dev, part);
		return 1;
	}
	return file_fat_detectfs();
}

U_BOOT_CMD(
	fatinfo,	3,	1,	do_fat_fsinfo,
	"print information about filesystem",
	"<interface> [<dev[:part]>]\n"
	"    - print information about filesystem from 'dev' on 'interface'"
);

#ifdef CONFIG_FAT_WRITE
static int do_fat_fswrite(cmd_tbl_t *cmdtp, int flag,
		int argc, char * const argv[])
{
	loff_t size;
	int ret;
	unsigned long addr;
	unsigned long count;
	struct blk_desc *dev_desc = NULL;
	disk_partition_t info;
	int dev = 0;
	int part = 1;
	void *buf;

	if (argc < 5)
		return cmd_usage(cmdtp);

	part = blk_get_device_part_str(argv[1], argv[2], &dev_desc, &info, 1);
	if (part < 0)
		return 1;

	dev = dev_desc->devnum;

	if (fat_set_blk_dev(dev_desc, &info) != 0) {
		printf("\n** Unable to use %s %d:%d for fatwrite **\n",
			argv[1], dev, part);
		return 1;
	}
	addr = simple_strtoul(argv[3], NULL, 16);
	count = (argc <= 5) ? 0 : simple_strtoul(argv[5], NULL, 16);

	buf = map_sysmem(addr, count);
	ret = file_fat_write(argv[4], buf, 0, count, &size);
	unmap_sysmem(buf);
	if (ret < 0) {
		printf("\n** Unable to write \"%s\" from %s %d:%d **\n",
			argv[4], argv[1], dev, part);
		return 1;
	}

	printf("%llu bytes written\n", size);

	return 0;
}

U_BOOT_CMD(
	fatwrite,	6,	0,	do_fat_fswrite,
	"write file into a dos filesystem",
	"<interface> <dev[:part]> <addr> <filename> [<bytes>]\n"
	"    - write file 'filename' from the address 'addr' in RAM\n"
	"      to 'dev' on 'interface'"
);
#endif



#define SECTOR_SIZE		512
#define DOS_PART_MAGIC_OFFSET	0x1fe



int do_fat_format(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	int dev = 0;
	int part = 1;
	char *ep;	
	struct blk_desc *dev_desc = NULL;
	disk_partition_t info = {};
	
	if (argc < 2) {
		printf ("usage : fatformat <interface> <dev[:part]>\n");
		return(0);
	}

	dev = (int)simple_strtoul (argv[2], &ep, 16);
	
	dev_desc = blk_get_dev(argv[1], dev);
		if (dev_desc == NULL) {
		puts ("\n ** Invalid boot device **\n");
		return 1;
	}
	part_init(dev_desc);
	
	if (*ep) {
		if (*ep != ':') {
			puts ("\n ** Invalid boot device, use 'dev[:part]' **\n");
			return 1;
		}
		part = (int)simple_strtoul(++ep, NULL, 16);
		if (part > 4 || part <1) {
			puts ("** Partition Number should be 1 ~ 4 **\n");
		}
	}


	//part_get_info;



	
	printf(" part_type  : %x \n",dev_desc->part_type);

	printf("Start format MMC%d partition%d ...\n", dev, part);
	
	//if (fat_format_device(dev_desc, part) != 0) {
	//	printf("Format failure!!!\n");
	//}
	/* 开始格式化分区 */
	unsigned char buffer[SECTOR_SIZE];
	int fat_size;
	/* check if we have a MBR (on floppies we have only a PBR) */
	if (blk_dread(dev_desc, 0, 1, (ulong *) buffer) != 1) {
		printf ("** Can't read from device**\n");
		return 1;
	}
	if (buffer[DOS_PART_MAGIC_OFFSET] != 0x55 ||
		buffer[DOS_PART_MAGIC_OFFSET + 1] != 0xaa) {
		printf("** MBR is broken **\n");
		/* no signature found */
		return 1;
	}
	part_get_info(dev_desc, part, &info);
	printf("sizeof(info.size) = %d",sizeof(info.size));
	printf("part size : %u",info.size);
	printf("part start : %u",info.start);
	if(info.size == 0)
	{
		printf("** 无法获取分区信息 **\n");
		return 1;
	}
	fat_size = write_pbr(dev_desc,&info);
	if(fat_size < 0)
		return 1;
	if(write_reserved(dev_desc, &info) < 0)
		return 1;
	if(write_fat(dev_desc, &info, fat_size) < 0)
		return 1;
	printf("Partition%d format complete.\n", part);
	
	return 0;
}

U_BOOT_CMD(
	fatformat, 3, 0, do_fat_format,
	"fatformat - disk format by FAT32\n",
	"fatformat <interface(only support mmc)> <dev:partition num>\n"
	"	- format by FAT32 on 'interface'\n"
);







