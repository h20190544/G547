#include <linux/module.h>
#include <linux/fs.h>	
#include <linux/types.h>	
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/usb.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/sched.h>

#define Nsectors 15633408 
#define USB_EP_IN 0x80
#define SCETORSIZE (512)
int c=0; //Variable for Major Number
int  sectsize = 512;
#define bio_iovec_idx(bio, idx)	(&((bio)->bi_io_vec[(idx)]))
#define __bio_kmap_atomic(bio, idx, kmtype)				\
	(kmap_atomic(bio_iovec_idx((bio), (idx))->bv_page) +	\
		bio_iovec_idx((bio), (idx))->bv_offset)
#define __bio_kunmap_atomic(addr, kmtype) kunmap_atomic(addr)

uint8_t endpoint_in = 0, endpoint_out = 0;
struct usb_device *device;

////////////////////////////////////// VID and PID of Registered Devices /////////////////////////////////////

#define SAN_VID  0x0781             // Vendor ID for SanDisk Storage Device
#define SAN_PID  0x5567             // Product ID for Sandisk Storage Device    

#define SONY_VID  0x054C			// Vendor ID for Sony Storage Device
#define SONY_PID  0x05BA			// Product ID for Sony Storage Device

///////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////Command Block Wrapper (CBW)////////////////////////////////////////////////////////
struct command_block_wrapper {
	uint8_t dCBWSignature[4];
	uint32_t dCBWTag;
	uint32_t dCBWDataTransferLength;
	uint8_t bmCBWFlags;
	uint8_t bCBWLUN;
	uint8_t bCBWCBLength;
	uint8_t CBWCB[16];
};
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////Command Status Wrapper (CSW)///////////////////////////////////////////////////////
struct command_status_wrapper {
	uint8_t dCSWSignature[4];
	uint32_t dCSWTag;
	uint32_t dCSWDataResidue;
	uint8_t bCSWStatus;
};
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static uint8_t cdb_length[256] = {
//	 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
	06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,  //  0
	06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,  //  1
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  2
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  3
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  4
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  5
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  6
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  7
	16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,  //  8
	16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,  //  9
	12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,  //  A
	12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,  //  B
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  C
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  D
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  E
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  F
};

/////////////////////////////////////////////////// Table of stoarge devices registered //////////////////////////////////////////////
static struct usb_device_id usbdev_table [] = 
{
	{USB_DEVICE(SAN_VID, SAN_PID)},
	{USB_DEVICE(SONY_VID , SONY_PID)},
	{} //terminating entry	
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct usbdev_private
{
	unsigned char class;
	unsigned char subclass;
	unsigned char protocol;
	unsigned char ep_in;
	unsigned char ep_out;
};

struct usbdev_private *p_usbdev_info;

struct mydiskdrive_dev 
{
	int size;                      /* Device size in sectors */
	u8 *data;                      /* The data array */
	short users;                   /* How many users */
	spinlock_t lock;               /* For mutual exclusion */
	struct request_queue *queue;   /* The device request queue */
	struct gendisk *gd;            /* The gendisk structure */
};

static struct mydiskdrive_dev *dev = NULL;

static void usbdev_disconnect(struct usb_interface *interface)
{
	printk(KERN_INFO "USBDEV Device Removed\n");
	struct gendisk *usb_disk = dev->gd;
	del_gendisk(usb_disk);
	blk_cleanup_queue(dev->queue);
	kfree(dev);
	return;
}


/////////////////////////////////////////////////////////////////// Function to send cbw /////////////////////////////////////////////

int send_mass_storage_command(struct usb_device *device, uint8_t endpoint, uint8_t lun,uint8_t *cdb, uint8_t direction,int data_length, uint32_t *ret_tag)  
/// this function wraps cdb in cbw and sends it over bulk endpoint endpoint
{
	uint32_t tag = 1;
	int r,size;
	uint8_t cdb_len;
	struct command_block_wrapper *cbw;
	cbw=(struct command_block_wrapper *)kmalloc(sizeof(struct command_block_wrapper),GFP_KERNEL);
	if (cdb == NULL) {
		return -1;
	}
	if (endpoint & USB_DIR_IN) {
		printk("send_mass_storage_command: cannot send command on IN endpoint\n");
		return -1;
	}	
	cdb_len = cdb_length[cdb[0]];
	if ((cdb_len == 0) || (cdb_len > sizeof(cbw->CBWCB))) {
		printk("Invalid command\n");
		return -1;
	}
	memset(cbw, 0, sizeof(*cbw));
	cbw->dCBWSignature[0] = 'U';
	cbw->dCBWSignature[1] = 'S';
	cbw->dCBWSignature[2] = 'B';
	cbw->dCBWSignature[3] = 'C';
	*ret_tag = tag;
	cbw->dCBWTag = tag++;
	cbw->dCBWDataTransferLength = data_length;
	cbw->bmCBWFlags = direction;
	cbw->bCBWLUN =lun;
	cbw->bCBWCBLength = cdb_len;
	memcpy(cbw->CBWCB, cdb, cdb_len);
	r = usb_bulk_msg(device,usb_sndbulkpipe(device,endpoint),(void*)cbw,31, &size,1000);
	if(r!=0)
		printk("Failed command transfer %d",r);
	return 0;
}

////////////////////////////////////////////// Device Status /////////////////////////////////////////////////

static int get_mass_storage_status(struct usb_device *device, uint8_t endpoint, uint32_t expected_tag)
{	
	int r,size;	
	struct command_status_wrapper *csw;
	csw=(struct command_status_wrapper *)kmalloc(sizeof(struct command_status_wrapper),GFP_KERNEL);
	r=usb_bulk_msg(device,usb_rcvbulkpipe(device,endpoint),(void*)csw,13, &size, 1000);
	if(r<0)
		printk("ERROR IN RECEIVING STATUS MESSAGE");
	if (size != 13) {
		printk("get_mass_storage_status: received %d bytes (expected 13)\n", size);
		return -1;
	}
	if (csw->dCSWTag != expected_tag) {
		printk("get_mass_storage_status: mismatched tags (expected %08X, received %08X)\n",
			expected_tag, csw->dCSWTag);
		return -1;
	}	
	printk(KERN_INFO "\nSTATUS: %02X (%s)\n", csw->bCSWStatus, csw->bCSWStatus?"FAILED":"Success");
	return 0;
}  

//////////////////////////////////////////////////////   Function where actual transfer is done ////////////////////////////////////////////

int usb_read_write(int write,char *buffer,int nsect,sector_t sector)
{
	int nbytes = nsect * sectsize;   // total bytes to read or write for each segment
	int result;
	unsigned int size;
	uint8_t lun=0;
	uint32_t expected_tag;
	uint8_t cdb[16];	// SCSI Command Descriptor Block the SCSI command that we want to send
	
	if(write)          // USB WRITE
	{
	
		printk(KERN_INFO"Writing:\n");
		
		memset(cdb,0,sizeof(cdb));
		cdb[0]=0x2A;                       // function descriptor for write
		cdb[2]=(sector>>24)&0xFF;		   // logical address of the sector (4 bytes)
		cdb[3]=(sector>>16)&0xFF;
		cdb[4]=(sector>>8)&0xFF;
		cdb[5]=(sector>>0)&0xFF;
		cdb[7]=(nsect>>8)&0xFF;            // transfer length in sectors (2 bytes)
		cdb[8]=(nsect>>0)&0xFF;	
		//cdb[8]=0x01;
		printk("sector = %d \n", sector );
		send_mass_storage_command(device,endpoint_out,lun,cdb,USB_DIR_OUT,nbytes,&expected_tag);
		result=usb_bulk_msg(device,usb_sndbulkpipe(device,endpoint_out),(void*)buffer,nbytes,&size, 1000);

	}
	else               //USB READ   
	{
	
		memset(cdb,0,sizeof(cdb));
		cdb[0] = 0x28;	                   // function descriptor for write
		cdb[2]=(sector>>24) & 0xFF;        // logical address of the sector (4 bytes)
		cdb[3]=(sector>>16) & 0xFF;
		cdb[4]=(sector>>8) & 0xFF;
		cdb[5]=(sector>>0) & 0xFF;
		cdb[7]=(nsect>>8) & 0xFF;          // transfer length in sectors (2 bytes)
		cdb[8]=(nsect>>0) & 0xFF;
		printk("sector = %d \n", sector );	
		send_mass_storage_command(device,endpoint_out,lun,cdb,USB_DIR_IN,nbytes,&expected_tag);
		result=usb_bulk_msg(device,usb_rcvbulkpipe(device,endpoint_in),(void*)buffer,nbytes,&size, 5000); 
	}
	get_mass_storage_status(device, endpoint_in,expected_tag);

	return 0; 
} 

static int usb_transfer(sector_t sector,sector_t nsect, char *buffer, int write)
{
    unsigned long offset = sector*512;
    unsigned long nbytes = nsect*512;
    if ((offset + nbytes) > (Nsectors*512)) {
        printk (KERN_NOTICE "Write Beyond Limit (%ld %ld)\n", offset, nbytes);
        return -1;
    }
 
 	usb_read_write(write,buffer,nsect,sector); 

	return 0; 
}  



static int send_data(struct request *req)
{
	int dir = rq_data_dir(req);
	int ret = 0;
	
	
	sector_t start_sector = blk_rq_pos(req); // starting sector for the operation

	unsigned int sector_cnt = blk_rq_sectors(req); // number of sectors on which op is to be done 
							
	struct bio_vec bv;

	#define BV_PAGE(bv) ((bv).bv_page)
	#define BV_OFFSET(bv) ((bv).bv_offset)
	#define BV_LEN(bv) ((bv).bv_len)

	struct req_iterator iter;
	sector_t sector_offset;
	int sectors;

	sector_t address;
	int i;
	
	sector_offset = 0;
	rq_for_each_segment(bv, req, iter)
	{
		char *buffer = __bio_kmap_atomic(req->bio, i, KM_USER0);
		if (BV_LEN(bv) % (sectsize) != 0)
		{
			printk(KERN_ERR"bio size is not a multiple ofsector size\n");
			ret = -EIO;
		}
		sectors = BV_LEN(bv) / sectsize;
	
		address = start_sector+sector_offset;
		
		usb_transfer(address,sectors,buffer, dir==WRITE);	
		
		sector_offset += sectors;
		__bio_kunmap_atomic(req->bio, KM_USER0);
	}
	
	if (sector_offset != sector_cnt)
	{
		printk(KERN_ERR "mydisk: bio info doesn't match with the request info");
		ret = -EIO;
	}

return ret;
}

////////////////////////////////////////////// Workqueue Structure /////////////////////////////////////////////////

static struct workqueue_struct *myqueue=NULL;
struct dev_work{   // customized structure 
	struct work_struct work; // kernel specific struct
	struct request *req;
 };


////////////////////////////////////////////// Deferred work Function /////////////////////////////////////////////////

void delayed_work_function(struct work_struct *work)
{
	//struct dev_work *mwp = NULL;
	struct dev_work *mwp = container_of(work, struct dev_work, work);
	struct request *req;
	req  = mwp->req;
	send_data(req);
	__blk_end_request_cur(mwp->req,0);
	kfree(mwp);
}

////////////////////////////////////////////// USB Request Function /////////////////////////////////////////////////
static void dev_request(struct request_queue *q)
{
	struct request *req;
	struct dev_work *usb_work = NULL;

	while ((req = blk_fetch_request(q)) != NULL) /*check active request 
						      *for data transfer*/
	{
		if(req == NULL && !blk_rq_is_passthrough(req)) // check if file sys req, for DRiver to handle 
			{
				printk(KERN_INFO "non FS request");
				__blk_end_request_all(req, -EIO);
				continue;
			}
		usb_work = (struct dev_work *)kmalloc(sizeof(struct dev_work), GFP_ATOMIC);
		if(usb_work == NULL )
		{
			printk(" memory allocation for deferred work  failed\n");
			__blk_end_request_all(req,0); // end the request
			continue;
		}
	usb_work->req = req;
	INIT_WORK(&usb_work->work , delayed_work_function);
	queue_work( myqueue, &usb_work->work);
	}
}


static int my_open(struct block_device *x, fmode_t mode)	 
{
   struct mydiskdrive_dev *dev = x->bd_disk->private_data;
    spin_lock(&dev->lock);
    if (! dev->users) 
        check_disk_change(x);	
    dev->users++;
    spin_unlock(&dev->lock);
    return 0;

}


static void my_release(struct gendisk *disk, fmode_t mode)
{

	printk(KERN_INFO "mydiskdrive : closed \n");
	struct mydiskdrive_dev *dev = disk->private_data;
	spin_lock(&dev->lock);
    	dev->users--;
    	spin_unlock(&dev->lock);

    	return ;
}


static struct block_device_operations fops =
{
	.owner = THIS_MODULE,
	.open = my_open,
	.release = my_release,
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static int usbdev_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	int i;
	unsigned char epAddr, epAttr;

	struct usb_endpoint_descriptor *ep_desc;
	device = interface_to_usbdev(interface);
	
	if((id->idProduct == SAN_PID && id->idVendor == SAN_VID) || (id->idProduct == SONY_PID && id->idVendor==SONY_VID))
	{
		printk(KERN_INFO "\nKnown USB drive detected \n");
	}
	
	
	else
	{
		printk(KERN_INFO "\nUnknown device plugged-in\n");
	}
	

	printk(KERN_INFO "Product ID is %04x \n",id->idProduct);
	printk(KERN_INFO "Vendor ID is %04x \n",id->idVendor);	
	printk(KERN_INFO "No. of Altsettings = %d\n",interface->num_altsetting);

	printk(KERN_INFO "No. of Endpoints = %d\n", interface->cur_altsetting->desc.bNumEndpoints);
	printk(KERN_INFO "USB DEVICE CLASS : %x", interface->cur_altsetting->desc.bInterfaceClass);
	printk(KERN_INFO "USB DEVICE SUB CLASS : %x", interface->cur_altsetting->desc.bInterfaceSubClass);
	printk(KERN_INFO "USB DEVICE Protocol : %x", interface->cur_altsetting->desc.bInterfaceProtocol);


	for(i=0;i<interface->cur_altsetting->desc.bNumEndpoints;i++)
	{
		ep_desc = &interface->cur_altsetting->endpoint[i].desc;
		epAddr = ep_desc->bEndpointAddress;
		epAttr = ep_desc->bmAttributes;
	
		if((epAttr & USB_ENDPOINT_XFERTYPE_MASK)==USB_ENDPOINT_XFER_BULK)
		{
			if(epAddr & USB_EP_IN)
			{	
				endpoint_in = epAddr;
				printk(KERN_INFO "EP %d is Bulk IN with address %d \n", i,endpoint_in);
			}		
	
			else
			{
				endpoint_out = epAddr; 		
				printk(KERN_INFO "EP %d is Bulk OUT with address %d\n", i,endpoint_out);
			}
		}

	}

	if((interface->cur_altsetting->desc.bInterfaceSubClass == 0x06) && (interface->cur_altsetting->desc.bInterfaceProtocol == 0x50))
		{
			printk(KERN_INFO "\nvalid UAS device is connected\n");
		}	
		else
		{
			printk(KERN_INFO "\nnon UAS supporting device is connected\n");
		}

	c = register_blkdev(c, "mydisk");// major no. allocation
	printk(KERN_ALERT "Major Number is : %d",c);

	dev = kmalloc(sizeof(struct mydiskdrive_dev), GFP_KERNEL);
		
	if(!dev)
	{
		printk("ENOMEM  at %d\n",__LINE__);
		return 0;
	}
	memset(dev, 0, sizeof(struct mydiskdrive_dev)); // for initializing all p_blkdev var as 0
	spin_lock_init(&dev->lock); // lock for queue
	dev->queue = blk_init_queue( dev_request, &dev->lock); 

	dev->gd = alloc_disk(2); // gendisk allocation
	(dev->gd)->major=c; // major no to gendisk
	dev->gd->first_minor=0; // first minor of gendisk
	//dev->gd->minors=2;
;
	dev->gd->fops = &fops;
	dev->gd->private_data = dev;
	dev->gd->queue = dev->queue;

	strcpy((dev->gd)->disk_name,"mydisk");
	set_capacity(dev->gd, Nsectors);  
	add_disk(dev->gd);

	return 0;
}

///////////////////////////////////////////////////////////operation structure//////////////////////////////////////////////////////////
static struct usb_driver usbdev_driver = {
	name: "my_usb_device",  //name of the device
	probe: usbdev_probe, // Whenever Device is plugged in
	disconnect: usbdev_disconnect, // When we remove a device
	id_table: usbdev_table, //  List of devices served by this driver
};

////////////////////////////////////////////////// init module //////////////////////////////////////////////////////

int block_init(void)
{
	usb_register(&usbdev_driver);
	printk(KERN_NOTICE "USB Read-Write Driver Inserted\n");
	printk(KERN_INFO "Registered disk\n"); 
	printk(KERN_ALERT "Work Queue defined\n");
	myqueue=create_workqueue("myqueue");  // my worker thread name
	return 0;	
}

////////////////////////////////////////////////// exit module //////////////////////////////////////////////////////

void block_exit(void)
{ 
	usb_deregister(&usbdev_driver);
	printk("USB Read-Write Driver unregistered");
	flush_workqueue(myqueue);  // to exit the work done
	destroy_workqueue(myqueue);
	return;
}


module_init(block_init);
module_exit(block_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nikhil Reddy, Tushar Patil");
MODULE_DESCRIPTION("USB Read-Write module");
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////