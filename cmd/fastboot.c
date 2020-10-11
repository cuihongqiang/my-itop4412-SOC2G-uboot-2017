/*
 * Copyright 2008 - 2009 Windriver, <www.windriver.com>
 * Author: Tom Rix <Tom.Rix@windriver.com>
 *
 * (C) Copyright 2014 Linaro, Ltd.
 * Rob Herring <robh@kernel.org>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */
#include <common.h>
#include <command.h>
#include <console.h>
#include <g_dnl.h>
#include <usb.h>
#include <linux/string.h>


static int do_fastboot(cmd_tbl_t *cmdtp, int flag, int argc, char *const argv[])
{
	int controller_index;
	char *usb_controller;
	int ret;

	if (argc < 2)
		return CMD_RET_USAGE;

	usb_controller = argv[1];
	controller_index = simple_strtoul(usb_controller, NULL, 0);

	ret = board_usb_init(controller_index, USB_INIT_DEVICE);
	if (ret) {
		pr_err("USB init failed: %d", ret);
		return CMD_RET_FAILURE;
	}

	g_dnl_clear_detach();
	ret = g_dnl_register("usb_dnl_fastboot");
	if (ret)
		return ret;

	if (!g_dnl_board_usb_cable_connected()) {
		puts("\rUSB cable not detected.\n" \
		     "Command exit.\n");
		ret = CMD_RET_FAILURE;
		goto exit;
	}

	while (1) {
		if (g_dnl_detach())
			break;
		if (ctrlc())
			break;
		usb_gadget_handle_interrupts(controller_index);
	}

	ret = CMD_RET_SUCCESS;

exit:
	g_dnl_unregister();
	g_dnl_clear_detach();
	board_usb_cleanup(controller_index, USB_INIT_DEVICE);

	return ret;
}

U_BOOT_CMD(
	fastboot, 2, 1, do_fastboot,
	"use USB Fastboot protocol",
	"<USB_controller>\n"
	"    - run as a fastboot usb device"
);







/* 添加flash命令，来完成对各分区的烧写 */

#define BOOT_PARTITION 0x01
#define USER_PARTITION 0x02


struct emmc_fake_partitions_item{
	char * part_name;
	lbaint_t start;
	lbaint_t size;
	int 	loca;
};
/* 在引导分区进行硬编码分区,这里都以512b为单位*/
struct emmc_fake_partitions_item emmc_fake_partitions[]={{
		.part_name = "bootloader",		/* 在引导分区 */
		.start	   = 0x0000,			
		.size	   = 0x0500,			/* 640Kb */
		.loca      = BOOT_PARTITION,
	},{
		.part_name = "env",
		.start	   = 0x0001,
		.size	   = 0x0020,			/* 16Kb */
		.loca      = USER_PARTITION,
	},{
		.part_name = "device_tree",		/* 设备树紧跟uboot参数 */
		.start	   = 0x0021,
		.size	   = 0x0100,			/* 128Kb */
		.loca	   = USER_PARTITION,
	},{
		.part_name = "kernel",			/* linux内核 */
		.start	   = 0x0200,
		.size	   = 0x2000,			/* 4Mb */
		.loca	   = USER_PARTITION,
	},{
		/* NULL */
	},

	
};



extern int do_flash_write(const char * part_name,unsigned long address);
extern int do_flash_read(const char * part_name,unsigned long address);

static int do_flash(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	unsigned long address;
	int err;
	if(argc != 4)
		return 1;
	address = simple_strtoul(argv[3], NULL, 16);
	
	printf("*%s*\n",argv[1]);
	
	if(!strcmp("write", argv[1]) )
	{	
		printf("烧写：\n");
		err = do_flash_write(argv[2], address);
	}
	else 
	if(!strcmp("read",argv[1]))
	{
		printf("读取");
		err = do_flash_read(argv[2], address);
	}
	else
	{
		printf("参数不对\n");
		err = -1;
	}
	
	return (err < 0)? 1 : 0;
}


U_BOOT_CMD(
	flash,  4,  0,   do_flash,
	"烧写 文件系统|内核|设备树|UBOOT",
	"\nwrite <kernel|device_tree|bootloader> <memory address> \n"
	"    -进行启动分区的烧写\n"
	"read <kernel|device_tree|bootloader> <memory address>\n"
	"    -把各种启动分区读到内存\n\n"
);















