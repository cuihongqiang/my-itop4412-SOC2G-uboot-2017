/*
 * Copyright 2014 Broadcom Corporation.
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <config.h>
#include <common.h>
#include <blk.h>
#include <fastboot.h>
#include <fb_mmc.h>
#include <image-sparse.h>
#include <part.h>
#include <mmc.h>
#include <div64.h>
#include <linux/compat.h>
#include <linux/string.h>
#include <android_image.h>

/*
 * FIXME: Ensure we always set these names via Kconfig once xxx_PARTITION is
 * migrated
 */
#ifndef CONFIG_FASTBOOT_GPT_NAME
#define CONFIG_FASTBOOT_GPT_NAME "gpt"
#endif


#ifndef CONFIG_FASTBOOT_MBR_NAME
#define CONFIG_FASTBOOT_MBR_NAME "mbr"
#endif

#define BOOT_PARTITION_NAME "boot"

struct fb_mmc_sparse {
	struct blk_desc	*dev_desc;
};

static int part_get_info_by_name_or_alias(struct blk_desc *dev_desc,
		const char *name, disk_partition_t *info)
{
	int ret;

	ret = part_get_info_by_name(dev_desc, name, info);
	if (ret < 0) {
		/* strlen("fastboot_partition_alias_") + 32(part_name) + 1 */
		char env_alias_name[25 + 32 + 1];
		char *aliased_part_name;

		/* check for alias */
		strcpy(env_alias_name, "fastboot_partition_alias_");
		strncat(env_alias_name, name, 32);
		aliased_part_name = env_get(env_alias_name);
		if (aliased_part_name != NULL)
			ret = part_get_info_by_name(dev_desc,
					aliased_part_name, info);
	}
	return ret;
}








static lbaint_t fb_mmc_sparse_write(struct sparse_storage *info,
		lbaint_t blk, lbaint_t blkcnt, const void *buffer)
{
	struct fb_mmc_sparse *sparse = info->priv;
	struct blk_desc *dev_desc = sparse->dev_desc;

	return blk_dwrite(dev_desc, blk, blkcnt, buffer);
}

static lbaint_t fb_mmc_sparse_reserve(struct sparse_storage *info,
		lbaint_t blk, lbaint_t blkcnt)
{
	return blkcnt;
}

static void write_raw_image(struct blk_desc *dev_desc, disk_partition_t *info,
		const char *part_name, void *buffer,
		unsigned int download_bytes)
{
	lbaint_t blkcnt;
	lbaint_t blks;

	/* determine number of blocks to write */
	blkcnt = ((download_bytes + (info->blksz - 1)) & ~(info->blksz - 1));
	blkcnt = lldiv(blkcnt, info->blksz);

	if (blkcnt > info->size) {
		pr_err("too large for partition: '%s'\n", part_name);
		fastboot_fail("too large for partition");
		return;
	}

	puts("Flashing Raw Image\n");

	blks = blk_dwrite(dev_desc, info->start, blkcnt, buffer);
	if (blks != blkcnt) {
		pr_err("failed writing to device %d\n", dev_desc->devnum);
		fastboot_fail("failed writing to device");
		return;
	}

	printf("........ wrote " LBAFU " bytes to '%s'\n", blkcnt * info->blksz,
	       part_name);
	fastboot_okay("");
}

#ifdef CONFIG_ANDROID_BOOT_IMAGE
/**
 * Read Android boot image header from boot partition.
 *
 * @param[in] dev_desc MMC device descriptor
 * @param[in] info Boot partition info
 * @param[out] hdr Where to store read boot image header
 *
 * @return Boot image header sectors count or 0 on error
 */
static lbaint_t fb_mmc_get_boot_header(struct blk_desc *dev_desc,
				       disk_partition_t *info,
				       struct andr_img_hdr *hdr)
{
	ulong sector_size;		/* boot partition sector size */
	lbaint_t hdr_sectors;		/* boot image header sectors count */
	int res;

	/* Calculate boot image sectors count */
	sector_size = info->blksz;
	hdr_sectors = DIV_ROUND_UP(sizeof(struct andr_img_hdr), sector_size);
	if (hdr_sectors == 0) {
		pr_err("invalid number of boot sectors: 0");
		fastboot_fail("invalid number of boot sectors: 0");
		return 0;
	}

	/* Read the boot image header */
	res = blk_dread(dev_desc, info->start, hdr_sectors, (void *)hdr);
	if (res != hdr_sectors) {
		pr_err("cannot read header from boot partition");
		fastboot_fail("cannot read header from boot partition");
		return 0;
	}

	/* Check boot header magic string */
	res = android_image_check_header(hdr);
	if (res != 0) {
		pr_err("bad boot image magic");
		fastboot_fail("boot partition not initialized");
		return 0;
	}

	return hdr_sectors;
}

/**
 * Write downloaded zImage to boot partition and repack it properly.
 *
 * @param dev_desc MMC device descriptor
 * @param download_buffer Address to fastboot buffer with zImage in it
 * @param download_bytes Size of fastboot buffer, in bytes
 *
 * @return 0 on success or -1 on error
 */
static int fb_mmc_update_zimage(struct blk_desc *dev_desc,
				void *download_buffer,
				unsigned int download_bytes)
{
	uintptr_t hdr_addr;			/* boot image header address */
	struct andr_img_hdr *hdr;		/* boot image header */
	lbaint_t hdr_sectors;			/* boot image header sectors */
	u8 *ramdisk_buffer;
	u32 ramdisk_sector_start;
	u32 ramdisk_sectors;
	u32 kernel_sector_start;
	u32 kernel_sectors;
	u32 sectors_per_page;
	disk_partition_t info;
	int res;

	puts("Flashing zImage\n");

	/* Get boot partition info */
	res = part_get_info_by_name(dev_desc, BOOT_PARTITION_NAME, &info);
	if (res < 0) {
		pr_err("cannot find boot partition");
		fastboot_fail("cannot find boot partition");
		return -1;
	}

	/* Put boot image header in fastboot buffer after downloaded zImage */
	hdr_addr = (uintptr_t)download_buffer + ALIGN(download_bytes, PAGE_SIZE);
	hdr = (struct andr_img_hdr *)hdr_addr;

	/* Read boot image header */
	hdr_sectors = fb_mmc_get_boot_header(dev_desc, &info, hdr);
	if (hdr_sectors == 0) {
		pr_err("unable to read boot image header");
		fastboot_fail("unable to read boot image header");
		return -1;
	}

	/* Check if boot image has second stage in it (we don't support it) */
	if (hdr->second_size > 0) {
		pr_err("moving second stage is not supported yet");
		fastboot_fail("moving second stage is not supported yet");
		return -1;
	}

	/* Extract ramdisk location */
	sectors_per_page = hdr->page_size / info.blksz;
	ramdisk_sector_start = info.start + sectors_per_page;
	ramdisk_sector_start += DIV_ROUND_UP(hdr->kernel_size, hdr->page_size) *
					     sectors_per_page;
	ramdisk_sectors = DIV_ROUND_UP(hdr->ramdisk_size, hdr->page_size) *
				       sectors_per_page;

	/* Read ramdisk and put it in fastboot buffer after boot image header */
	ramdisk_buffer = (u8 *)hdr + (hdr_sectors * info.blksz);
	res = blk_dread(dev_desc, ramdisk_sector_start, ramdisk_sectors,
			ramdisk_buffer);
	if (res != ramdisk_sectors) {
		pr_err("cannot read ramdisk from boot partition");
		fastboot_fail("cannot read ramdisk from boot partition");
		return -1;
	}

	/* Write new kernel size to boot image header */
	hdr->kernel_size = download_bytes;
	res = blk_dwrite(dev_desc, info.start, hdr_sectors, (void *)hdr);
	if (res == 0) {
		pr_err("cannot writeback boot image header");
		fastboot_fail("cannot write back boot image header");
		return -1;
	}

	/* Write the new downloaded kernel */
	kernel_sector_start = info.start + sectors_per_page;
	kernel_sectors = DIV_ROUND_UP(hdr->kernel_size, hdr->page_size) *
				      sectors_per_page;
	res = blk_dwrite(dev_desc, kernel_sector_start, kernel_sectors,
			 download_buffer);
	if (res == 0) {
		pr_err("cannot write new kernel");
		fastboot_fail("cannot write new kernel");
		return -1;
	}

	/* Write the saved ramdisk back */
	ramdisk_sector_start = info.start + sectors_per_page;
	ramdisk_sector_start += DIV_ROUND_UP(hdr->kernel_size, hdr->page_size) *
					     sectors_per_page;
	res = blk_dwrite(dev_desc, ramdisk_sector_start, ramdisk_sectors,
			 ramdisk_buffer);
	if (res == 0) {
		pr_err("cannot write back original ramdisk");
		fastboot_fail("cannot write back original ramdisk");
		return -1;
	}

	puts("........ zImage was updated in boot partition\n");
	fastboot_okay("");
	return 0;
}
#endif




/* add by simon */
extern int mmc_send_cmd(struct mmc *mmc, struct mmc_cmd *cmd,
			struct mmc_data *data);
struct emmc_fake_partitions_item{
	char * part_name;
	lbaint_t start;
	lbaint_t size;
	int 	loca;
};
/* 在引导分区进行硬编码分区 */
extern struct emmc_fake_partitions_item emmc_fake_partitions[];


static int emmc_boot_open(struct mmc *mmc)
{
	int err;
	struct mmc_cmd cmd;

	/* Boot ack enable, boot partition enable , boot partition access */
	cmd.cmdidx = MMC_CMD_SWITCH;
	cmd.resp_type = MMC_RSP_R1b;
	cmd.cmdarg = ((3<<24)|(179<<16)|(((1<<6)|(1<<3)|(1<<0))<<8));


	err = mmc_send_cmd(mmc, &cmd, NULL);
	udelay(1000);
	if (err)
		return err;

	/* 4bit transfer mode at booting time. */
	cmd.cmdidx = MMC_CMD_SWITCH;
	cmd.resp_type = MMC_RSP_R1b;
	cmd.cmdarg = ((3<<24)|(177<<16)|(((1<<0)|(0<<2))<<8));//ly

	err = mmc_send_cmd(mmc, &cmd, NULL);
	udelay(1000);
	if (err)
		return err;

	return 0;
}

static int emmc_boot_close(struct mmc *mmc)
{
	int err;
	struct mmc_cmd cmd;

	/* Boot ack enable, boot partition enable , boot partition access */
	cmd.cmdidx = MMC_CMD_SWITCH;
	cmd.resp_type = MMC_RSP_R1b;
	cmd.cmdarg = ((3<<24)|(179<<16)|(((1<<6)|(1<<3)|(0<<0))<<8));

	err = mmc_send_cmd(mmc, &cmd, NULL);
	udelay(1000);
	if (err)
		return err;

	return 0;
}

#define EMMC_BLK_SIZE 512
#define BOOT_PARTITION 0x01
#define USER_PARTITION 0x02

static int part_get_itop4412_emmc_info_by_name(const char *name, disk_partition_t *info)
{
	static struct emmc_fake_partitions_item *item = emmc_fake_partitions;
	if(info == NULL)
		return -1;
	while(item->part_name){
		if(!strcmp(item->part_name,name))
		{
			info->blksz = EMMC_BLK_SIZE;
			info->start = item->start;
			info->size  = item->size;
			return item->loca;
		}
		item++;
	}
	return -1;
}

int do_flash_write(const char * part_name,unsigned long address)
{
	disk_partition_t info;
	struct mmc *mmc_dev;	
	struct blk_desc *dev_desc;
	int part;
	unsigned long n;
	
	dev_desc = blk_get_dev("mmc", CONFIG_FASTBOOT_FLASH_MMC_DEV);
	mmc_dev = find_mmc_device(CONFIG_FASTBOOT_FLASH_MMC_DEV);
	if(mmc_dev == NULL)
	{
		printf("** 获取mmc_dev失败** \n");
		return -1;
	}
	part = part_get_itop4412_emmc_info_by_name(part_name,&info);
	if(part < 0)
	{
		printf("没有这个分区\n");
		return -1;
	}
	if(part == BOOT_PARTITION && emmc_boot_open(mmc_dev)!=0)
	{
		printf("打开引导分区失败\n");
		return -1;
	}

	
	printf("待烧写 %u blk\n",info.size);
	n = blk_dwrite(dev_desc, info.start , info.size, (void*)address);
	printf("已经烧写 %u blk\n",n);
	
	if(part == BOOT_PARTITION && emmc_boot_close(mmc_dev)!=0)
	{
		printf("关闭引导分区失败\n");
		return -1;
	}

	if(n != info.size)
	{
		printf("** emmc写错误\n");
		return -1;
	}
	printf("烧写成功....\n");
	return 0;

}


int do_flash_read(const char * part_name,unsigned long address)
{
	disk_partition_t info;
	struct mmc *mmc_dev;
	int part;
	unsigned long n;
	mmc_dev = find_mmc_device(CONFIG_FASTBOOT_FLASH_MMC_DEV);
	if(mmc_dev == NULL)
	{
		printf("** 获取mmc_dev失败** \n");
		return -1;
	}
	part = part_get_itop4412_emmc_info_by_name(part_name,&info);
	if(part < 0)
	{
		printf("没有这个分区\n");
		return -1;
	}
	
	if(part == BOOT_PARTITION && emmc_boot_open(mmc_dev)!=0)
	{
		printf("打开引导分区失败\n");
		return -1;
	}

	printf("待读 %ublk\n",info.size);
	
	n = blk_dread(mmc_get_blk_desc(mmc_dev), info.start , info.size, (void*)address);
	printf("已读 %ublk\n",n);

	if(part == BOOT_PARTITION && emmc_boot_close(mmc_dev)!=0)
	{
		printf("关闭引导分区失败\n");
		return -1;
	}


	if(n != info.size)
	{
		printf("** emmc读错误\n");
		return -1;
	}
	printf("读成功.....\n");
	return 0;

}




void fb_mmc_flash_write(const char *cmd, void *download_buffer,
			unsigned int download_bytes)
{
	struct blk_desc *dev_desc;
	disk_partition_t info;

	dev_desc = blk_get_dev("mmc", CONFIG_FASTBOOT_FLASH_MMC_DEV);
	if (!dev_desc || dev_desc->type == DEV_TYPE_UNKNOWN) {
		pr_err("invalid mmc device\n");
		fastboot_fail("invalid mmc device");
		return;
	}

#if CONFIG_IS_ENABLED(EFI_PARTITION)
	if (strcmp(cmd, CONFIG_FASTBOOT_GPT_NAME) == 0) {
		printf("%s: updating MBR, Primary and Backup GPT(s)\n",
		       __func__);
		if (is_valid_gpt_buf(dev_desc, download_buffer)) {
			printf("%s: invalid GPT - refusing to write to flash\n",
			       __func__);
			fastboot_fail("invalid GPT partition");
			return;
		}
		if (write_mbr_and_gpt_partitions(dev_desc, download_buffer)) {
			printf("%s: writing GPT partitions failed\n", __func__);
			fastboot_fail("writing GPT partitions failed");
			return;
		}
		printf("........ success\n");
		fastboot_okay("");
		return;
	}
#endif

#if CONFIG_IS_ENABLED(DOS_PARTITION)
	if (strcmp(cmd, CONFIG_FASTBOOT_MBR_NAME) == 0) {
		printf("%s: updating MBR\n", __func__);
		if (is_valid_dos_buf(download_buffer)) {
			printf("%s: invalid MBR - refusing to write to flash\n",
			       __func__);
			fastboot_fail("invalid MBR partition");
			return;
		}
		if (write_mbr_partition(dev_desc, download_buffer)) {
			printf("%s: writing MBR partition failed\n", __func__);
			fastboot_fail("writing MBR partition failed");
			return;
		}
		printf("........ success\n");
		fastboot_okay("");
		return;
	}
#endif

#ifdef CONFIG_ANDROID_BOOT_IMAGE
	if (strncasecmp(cmd, "zimage", 6) == 0) {
		fb_mmc_update_zimage(dev_desc, download_buffer, download_bytes);
		return;
	}
#endif

#ifndef CONFIG_ITOP4412
	/* 寻找falsh,所指定的分区 */
	if (part_get_info_by_name_or_alias(dev_desc, cmd, &info) < 0) {
		pr_err("cannot find partition: '%s'\n", cmd);
		fastboot_fail("cannot find partition");
		return;
	}
#else	/* CONFIG_ITOP4412 */
	/* 写死在emmc中的分区*/	

	/*		主要对他们进行赋值
	 *		info.blksz;	
	 *		info.start;	
	 *		info.size;	
	 */
	int error;
	struct mmc * mmc_dev=NULL;
	mmc_dev = find_mmc_device(CONFIG_FASTBOOT_FLASH_MMC_DEV);
	if(mmc_dev == NULL)
	{
		printf("** 获取mmc_dev失败** \n");
		return ;
	}
	
	error = part_get_itop4412_emmc_info_by_name(cmd,&info);
	if(error < 0)
	{
		printf("cannot find partition: '%s'\n", cmd);
		fastboot_fail("cannot find partition");
		return;
	}
	if(error == BOOT_PARTITION && emmc_boot_open(mmc_dev)!=0)
	{
		printf("打开引导分区失败\n");
		return ;
	}
	printf("打开获取mmc %d \n",CONFIG_FASTBOOT_FLASH_MMC_DEV);

#endif /* CONFIG_ITOP4412 */


	if (is_sparse_image(download_buffer)) {
		/* 对于压缩的image */
		struct fb_mmc_sparse sparse_priv;
		struct sparse_storage sparse;

		sparse_priv.dev_desc = dev_desc;

		sparse.blksz = 	info.blksz;					/* 块大小 */
		sparse.start = 	info.start;					/* 分区开始块 */
		sparse.size = 	info.size;					/* 分区大小 */
		sparse.write = fb_mmc_sparse_write;			/* 写mmc的函数 */
		sparse.reserve = fb_mmc_sparse_reserve;		/* 没懂 */

		printf("Flashing sparse image at offset " LBAFU "\n",
		       sparse.start);

		sparse.priv = &sparse_priv;
		write_sparse_image(&sparse, cmd, download_buffer,
				   download_bytes);
	} else {
		/* 对于未压缩的image */
		write_raw_image(dev_desc, &info, cmd, download_buffer,
				download_bytes);
	}
#ifdef CONFIG_ITOP4412
	if(error == BOOT_PARTITION && emmc_boot_close(mmc_dev)!=0)
	{
		printf("关闭引导分区失败\n");
		return ;
	}
	
	printf("关闭mmc %d \n",CONFIG_FASTBOOT_FLASH_MMC_DEV);
#endif

	
	
}

void fb_mmc_erase(const char *cmd)
{
	int ret;
	struct blk_desc *dev_desc;
	disk_partition_t info;
	lbaint_t blks, blks_start, blks_size, grp_size;
	struct mmc *mmc = find_mmc_device(CONFIG_FASTBOOT_FLASH_MMC_DEV);

	if (mmc == NULL) {
		pr_err("invalid mmc device");
		fastboot_fail("invalid mmc device");
		return;
	}

	dev_desc = blk_get_dev("mmc", CONFIG_FASTBOOT_FLASH_MMC_DEV);
	if (!dev_desc || dev_desc->type == DEV_TYPE_UNKNOWN) {
		pr_err("invalid mmc device");
		fastboot_fail("invalid mmc device");
		return;
	}

	ret = part_get_info_by_name_or_alias(dev_desc, cmd, &info);
	if (ret < 0) {
		pr_err("cannot find partition: '%s'", cmd);
		fastboot_fail("cannot find partition");
		return;
	}

	/* Align blocks to erase group size to avoid erasing other partitions */
	grp_size = mmc->erase_grp_size;
	blks_start = (info.start + grp_size - 1) & ~(grp_size - 1);
	if (info.size >= grp_size)
		blks_size = (info.size - (blks_start - info.start)) &
				(~(grp_size - 1));
	else
		blks_size = 0;

	printf("Erasing blocks " LBAFU " to " LBAFU " due to alignment\n",
	       blks_start, blks_start + blks_size);

	blks = blk_derase(dev_desc, blks_start, blks_size);
	if (blks != blks_size) {
		pr_err("failed erasing from device %d", dev_desc->devnum);
		fastboot_fail("failed erasing from device");
		return;
	}

	printf("........ erased " LBAFU " bytes from '%s'\n",
	       blks_size * info.blksz, cmd);
	fastboot_okay("");
}
