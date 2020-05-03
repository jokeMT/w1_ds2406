/*
 * w1_ds2406.c - w1 family 12 (DS2406) driver
 * based on w1_ds2413.c by Mariusz Bialonczyk <manio@skyboo.net>
 *
 * Copyright (c) 2014 Scott Alfter <scott@alfter.us>
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2. See the file COPYING for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/crc16.h>
#include <linux/uaccess.h>
#include <linux/w1.h>

#define W1_FAMILY_DS2406	0x12

#define W1_F12_FUNC_READ_STATUS		   0xAA
#define W1_F12_FUNC_WRITE_STATUS	   0x55
#define W1_F12_FUNC_READ_CHANNEL	   0xf5

static ssize_t w1_f12_read_channels(
	struct file *filp, struct kobject *kobj,
	struct bin_attribute *bin_attr,
	char *buf, loff_t off, size_t count)
{
	char outStr[1024];
	bool supply_indication=false;
	bool number_of_channels=false;
	bool piob_leach=false;
	bool pioa_leach=false;
	bool piob_sensed=false;
	bool pioa_sensed=false;
	bool piob_flfoq=false;
	bool pioa_flfoq=false;

	u8 w1_buf[13]={W1_F12_FUNC_READ_CHANNEL, (unsigned char)0xb01000101, 0xFF, 0, 0,0,0,0,0,0,0,0,0};
	struct w1_slave *sl = kobj_to_w1_slave(kobj);
	u16 crc=0;
	u16 crc_slave=0;
	int i;
	ssize_t rtnval=9;
	u16 tries=3;
	if (off != 0)
		return 0;
	if (!buf)
		return -EINVAL;

	mutex_lock(&sl->master->bus_mutex);

	while(tries--){
		if (w1_reset_select_slave(sl)) {
			rtnval=-EIO;
			continue;
		}
		w1_next_pullup(sl->master, 5);
		w1_write_block(sl->master, w1_buf, 3);
		mdelay(5);
		w1_read_block(sl->master, w1_buf+3, 4);
	
		crc = (~crc16(0x00, w1_buf,5 ));
		crc_slave = w1_buf[5] | (w1_buf[6]<<8);
		if (crc_slave==crc ){
			supply_indication=(w1_buf[3]&0x40)>1;
			number_of_channels=(w1_buf[3]&0x30)>1;
			piob_leach=(w1_buf[3]&0x20)>1;
			pioa_leach=(w1_buf[3]&0x10)>1;
			piob_sensed=(w1_buf[3]&0x08)>1;
			pioa_sensed=(w1_buf[3]&0x04)>1;
			piob_flfoq=(w1_buf[3]&0x02)>1;
			pioa_flfoq=(w1_buf[3]&0x01)>1;

			snprintf(buf,9,"%d%d%d%d%d%d%d%d\n",supply_indication,number_of_channels,pioa_sensed,pioa_leach,pioa_flfoq,piob_sensed,piob_leach,piob_flfoq);
 			rtnval=9;
				//printk(KERN_INFO "DS2406 crc ok: %04x != %04x \n",crc,crc_slave);
break;
		} else {
			rtnval=-EIO;
			printk(KERN_INFO "DS2406 crc err: %04x != %04x \n",crc,crc_slave);
		}
		mdelay(40);
	}
	mutex_unlock(&sl->master->bus_mutex);

	return rtnval;
}

static ssize_t w1_f12_read_state(
	struct file *filp, struct kobject *kobj,
	struct bin_attribute *bin_attr,
	char *buf, loff_t off, size_t count)
{
	u8 w1_buf[6]={W1_F12_FUNC_READ_STATUS, 7, 0, 0, 0, 0};
	struct w1_slave *sl = kobj_to_w1_slave(kobj);
	u16 crc=0;
	int i;
	ssize_t rtnval=1;

	if (off != 0)
		return 0;
	if (!buf)
		return -EINVAL;

	mutex_lock(&sl->master->bus_mutex);

	if (w1_reset_select_slave(sl)) {
		mutex_unlock(&sl->master->bus_mutex);
		return -EIO;
	}

	w1_write_block(sl->master, w1_buf, 3);
	w1_read_block(sl->master, w1_buf+3, 3);
	for (i=0; i<6; i++)
		crc=crc16_byte(crc, w1_buf[i]);
    printk(KERN_INFO "Hello world!%d\n",w1_buf[3]);
	if (crc==0xb001) /* good read? */
		*buf=((w1_buf[3]>>5)&3)|0x30;
	else
		rtnval=-EIO;

	mutex_unlock(&sl->master->bus_mutex);

	return rtnval;
}

static ssize_t w1_f12_write_output(
	struct file *filp, struct kobject *kobj,
	struct bin_attribute *bin_attr,
	char *buf, loff_t off, size_t count)
{
	struct w1_slave *sl = kobj_to_w1_slave(kobj);
	u8 w1_buf[6]={W1_F12_FUNC_WRITE_STATUS, 7, 0, 0, 0, 0};
	u16 crc=0;
	int i;
	ssize_t rtnval=1;

	if (count != 1 || off != 0)
		return -EFAULT;

	mutex_lock(&sl->master->bus_mutex);

	if (w1_reset_select_slave(sl)) {
		mutex_unlock(&sl->master->bus_mutex);
		return -EIO;
	}

	w1_buf[3] = (((*buf)&3)<<5)|0x1F;
	w1_write_block(sl->master, w1_buf, 4);
	w1_read_block(sl->master, w1_buf+4, 2);
	for (i=0; i<6; i++)
		crc=crc16_byte(crc, w1_buf[i]);
	if (crc==0xb001) /* good read? */
		w1_write_8(sl->master, 0xFF);
	else
		rtnval=-EIO;

	mutex_unlock(&sl->master->bus_mutex);
	return rtnval;
}

#define NB_SYSFS_BIN_FILES 3
static struct bin_attribute w1_f12_sysfs_bin_files[NB_SYSFS_BIN_FILES] = {
	{
		.attr = {
			.name = "state",
			.mode = S_IRUGO,
		},
		.size = 1,
		.read = w1_f12_read_state,
	},
	{
		.attr = {
			.name = "channel",
			.mode = S_IRUGO,
		},
		.size = 9,
		.read = w1_f12_read_channels,
	},
	{
		.attr = {
			.name = "output",
			.mode = S_IRUGO | S_IWUSR | S_IWGRP,
		},
		.size = 1,
		.write = w1_f12_write_output,
	}
};

static int w1_f12_add_slave(struct w1_slave *sl)
{
	int err = 0;
	int i;

	for (i = 0; i < NB_SYSFS_BIN_FILES && !err; ++i)
		err = sysfs_create_bin_file(
			&sl->dev.kobj,
			&(w1_f12_sysfs_bin_files[i]));
	if (err)
		while (--i >= 0)
			sysfs_remove_bin_file(&sl->dev.kobj,
				&(w1_f12_sysfs_bin_files[i]));
	return err;
}

static void w1_f12_remove_slave(struct w1_slave *sl)
{
	int i;
	for (i = NB_SYSFS_BIN_FILES - 1; i >= 0; --i)
		sysfs_remove_bin_file(&sl->dev.kobj,
			&(w1_f12_sysfs_bin_files[i]));
}

static struct w1_family_ops w1_f12_fops = {
	.add_slave      = w1_f12_add_slave,
	.remove_slave   = w1_f12_remove_slave,
};

static struct w1_family w1_family_12 = {
	.fid = W1_FAMILY_DS2406,
	.fops = &w1_f12_fops,
};
module_w1_family(w1_family_12);

MODULE_AUTHOR("Scott Alfter <scott@alfter.us>");
MODULE_DESCRIPTION("w1 family 12 driver for DS2406 2 Pin IO");
MODULE_LICENSE("GPL");
