/*
 * (C) Copyright 2011 - 2012 Samsung Electronics
 * EXT4 filesystem implementation in Uboot by
 * Uma Shankar <uma.shankar@samsung.com>
 * Manjunatha C Achar <a.manjunatha@samsung.com>

 * (C) Copyright 2004
 * esd gmbh <www.esd-electronics.com>
 * Reinhard Arlt <reinhard.arlt@esd-electronics.com>
 *
 * made from cmd_reiserfs by
 *
 * (C) Copyright 2003 - 2004
 * Sysgo Real-Time Solutions, AG <www.elinos.com>
 * Pavel Bartusek <pba@sysgo.com>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

/*
 * Ext2fs support
 */
#include <fs.h>



int ext2fs_format(struct blk_desc *dev_desc, int part_no, char set_journaling);







static int do_ext2ls(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	return do_ls(cmdtp, flag, argc, argv, FS_TYPE_EXT);
}

/******************************************************************************
 * Ext2fs boot command intepreter. Derived from diskboot
 */
int do_ext2load(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	return do_load(cmdtp, flag, argc, argv, FS_TYPE_EXT);
}

U_BOOT_CMD(
	ext2ls,	4,	1,	do_ext2ls,
	"list files in a directory (default /)",
	"<interface> <dev[:part]> [directory]\n"
	"    - list files from 'dev' on 'interface' in a 'directory'"
);

U_BOOT_CMD(
	ext2load,	6,	0,	do_ext2load,
	"load binary file from a Ext2 filesystem",
	"<interface> [<dev[:part]> [addr [filename [bytes [pos]]]]]\n"
	"    - load binary file 'filename' from 'dev' on 'interface'\n"
	"      to address 'addr' from ext2 filesystem."
);


enum _FS_TYPE{
	FS_TYPE_EXT2,
	FS_TYPE_EXT3,
	FS_TYPE_EXT4
};


int ext_format (int argc, char * const argv[], char filesystem_type)
{
	int dev = 0;
	int part = 1;
	char *ep;
	struct blk_desc *dev_desc = NULL;

	if(argc < 2) {
		printf("usage : ext2format <interface> <dev[:part]>\n");
		return (0);
	}
	dev = (int)simple_strtoul(argv[2], &ep, 16);	
	dev_desc = blk_get_dev(argv[1], dev);
	
	if (dev_desc == NULL) {
		puts("\n** Invalid boot device **\n");
		return 1;
	}
	if(*ep) {
		if(*ep != ':') {
			puts ("\n** Invalid boot device, use 'dev[:part]' **\n");
			return 1;
		}
		part = (int)simple_strtoul(++ep, NULL, 16);
		if(part > 4 || part <1) {
			puts ("** Partition Number shuld be 1 ~ 4 **\n");
			return 1;
		}
	}
	printf("Start format MMC%d partition%d ....\n", dev, part);
	
	switch (filesystem_type) {
	case FS_TYPE_EXT3:
	case FS_TYPE_EXT2:
		if (ext2fs_format(dev_desc, part, filesystem_type) != 0)
			printf("Format failure!!!\n");
		break;

	default:
		printf("FileSystem Type Value is not invalidate=%d \n", filesystem_type);
		break;
	}
	return 0;
}







int do_ext2_format(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	return ext_format(argc, argv, FS_TYPE_EXT2);
}

int do_ext3_format(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	return ext_format(argc, argv, FS_TYPE_EXT3);
}



U_BOOT_CMD(
	ext2format, 3, 0, do_ext2_format,
	"ext2format - disk format by ext2\n",
	"<interface (only support mmc)> <dev:partition num>\n"
	"	- format by ext2 on 'interface'\n"
);


U_BOOT_CMD(
	ext3format, 3, 0, do_ext3_format,
	"ext3format - disk format bt ext3\n",
	"	- format by ext3 on 'interface'\n"
);




