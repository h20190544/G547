#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>                 
#include <linux/uaccess.h>              
#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/random.h>
#include <linux/ioctl.h>


#define MAGIC_NUM 'A'

#define CHANNEL 1
#define ALLIGNMENT 2

#define SET_CHANNEL _IOW(MAGIC_NUM, CHANNEL, unsigned int)
#define SET_ALLIGNMENT _IOW(MAGIC_NUM, ALLIGNMENT, unsigned int)

static dev_t devnum; // variable for device number
static struct cdev adc; // variable for the adc
static struct class *cls; // variable for the device class

uint16_t i;
uint16_t channel;
uint16_t allignment;


static int my_open(struct inode *i, struct file *f)
{
	printk(KERN_INFO "my_adc : open()\n");
	return 0;
}

static int my_close(struct inode *i, struct file *f)
{
	printk(KERN_INFO "my_adc : close()\n");
	return 0;
}

static ssize_t my_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
	printk(KERN_INFO "my_adc : read()\n");
	get_random_bytes(&i, 2);
	i=i%100;
	printk(KERN_INFO "%d\n",i);
	printk(KERN_INFO "ADC_Value: %d", i);
	printk(KERN_INFO "Channel : %d \n",channel);
	printk(KERN_INFO "Allignment : %d \n",allignment);
	if(allignment == 1)
	{
		i = i << 6;
	}
	copy_to_user(buf, &i, 2);
	return 0;
}

static long my_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	switch(cmd)
	{
		case SET_CHANNEL:
			copy_from_user(&channel ,(int32_t*) arg, sizeof(channel));
			printk(KERN_INFO "Channel : %d \n",channel);
			break;
		case SET_ALLIGNMENT:
			copy_from_user(&allignment ,(int32_t*) arg, sizeof(allignment));
			printk(KERN_INFO "Allignment : %d \n",allignment);
			break;
	}
	return 0;
}




static struct file_operations fops =
{
  .owner 	= THIS_MODULE,
  .open 	= my_open,
  .release 	= my_close,
  .read 	= my_read,
  .unlocked_ioctl= my_ioctl,
};
 

static int __init mychar_init(void) 
{
	printk(KERN_INFO "ADC char driver is registered");
	
	// STEP 1 : reserve <major, minor>
	if (alloc_chrdev_region(&devnum, 0, 1, "adc8") < 0)
	{
		return -1;
	}
	
	// STEP 2 : creation of device file
    if ((cls = class_create(THIS_MODULE, "chardev")) == NULL)
	{
		unregister_chrdev_region(devnum, 1);
		return -1;
	}
    if (device_create(cls, NULL, devnum, NULL, "adc8") == NULL)
	{
		class_destroy(cls);
		unregister_chrdev_region(devnum, 1);
		return -1;
	}
	
	// STEP 3 : Link fops and cdev to device node
    cdev_init(&adc, &fops);
    if (cdev_add(&adc, devnum, 1) == -1)
	{
		device_destroy(cls, devnum);
		class_destroy(cls);
		unregister_chrdev_region(devnum, 1);
		return -1;
	}
	return 0;
}
 
static void __exit mychar_exit(void) 
{
	cdev_del(&adc);
	device_destroy(cls, devnum);
	class_destroy(cls);
	unregister_chrdev_region(devnum, 1);
	printk(KERN_INFO "ADC char driver unregistered\n\n");
}
 
module_init(mychar_init);
module_exit(mychar_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("NIKHIL");
MODULE_DESCRIPTION("Driver for ADC");
