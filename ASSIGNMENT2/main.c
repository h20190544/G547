#include<linux/kernel.h>
#include<linux/module.h>
#include<linux/usb.h>
#include <linux/slab.h> 


#define SAN_VID 0x0781      // device pid and vid
#define SAN_PID 0x5567

#define READ_CAPACITY_LENGTH          0x08
#define be_to_int32(buf) (((buf)[0]<<24)|((buf)[1]<<16)|((buf)[2]<<8)|(buf)[3])
#define LIBUSB_ENDPOINT_IN          0x80
#define LIBUSB_REQUEST_TYPE_CLASS    (0x01<<5)
#define LIBUSB_RECIPIENT_INTERFACE   0x01
#define be_to_int32(buf) (((buf)[0]<<24)|((buf)[1]<<16)|((buf)[2]<<8)|(buf)[3])
#define BOMS_RESET                    0xFF
#define BOMS_RESET_REQ_TYPE           0x21
#define BOMS_GET_MAX_LUN              0xFE
#define BOMS_GET_MAX_LUN_REQ_TYPE     0xA1
#define REQUEST_DATA_LENGTH           0x12
#define LIBUSB_ERROR_PIPE          -9
#define LIBUSB_SUCCESS  0


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

struct command_block_wrapper {
	uint8_t dCBWSignature[4];
	uint32_t dCBWTag;
	uint32_t dCBWDataTransferLength;
	uint8_t bmCBWFlags;
	uint8_t bCBWLUN;
	uint8_t bCBWCBLength;
	uint8_t CBWCB[16];
};



static void usbdev_disconnect(struct usb_interface *interface)
{
	printk(KERN_INFO "USBDEV Device Removed\n");
	return;
}

static struct usb_device_id usbdev_table [] = {
	{USB_DEVICE(SAN_VID, SAN_PID)},
	{} /*terminating entry*/	
};

static int usbdev_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	struct usb_device *device;
	device = interface_to_usbdev(interface);
                                                      
	unsigned char epAddr, epAttr;                //endpoint address and attribute
	struct usb_endpoint_descriptor *ep_desc;
	int i;
 	uint8_t endpoint_in=0,endpoint_out=0;

	
	printk("knoum USB drive detected\n");
	printk("PID=%04x,VID=%04x\n", id->idProduct,id->idVendor);


	printk(KERN_INFO "No. of Altsettings = %d\n",interface->num_altsetting);

	printk(KERN_INFO "No. of Endpoints = %d\n", interface->cur_altsetting->desc.bNumEndpoints);

	for(i=0;i<interface->cur_altsetting->desc.bNumEndpoints;i++)
	{
		ep_desc = &interface->cur_altsetting->endpoint[i].desc;
		epAddr = ep_desc->bEndpointAddress;
		epAttr = ep_desc->bmAttributes;
	
		if((epAttr & USB_ENDPOINT_XFERTYPE_MASK)==USB_ENDPOINT_XFER_BULK)
		{
			if(epAddr & 0x80)
			{
				printk(KERN_INFO "EP %d is Bulk IN\n", i);
				endpoint_in = ep_desc->bEndpointAddress;
			}
			else
			{
				printk(KERN_INFO "EP %d is Bulk OUT\n", i);
				endpoint_out = ep_desc->bEndpointAddress;
			}
		}
	
		if((epAttr & USB_ENDPOINT_XFERTYPE_MASK)==USB_ENDPOINT_XFER_CONTROL)
		{
			if(epAddr & 0x80)
			{
				printk(KERN_INFO "EP %d is Control IN\n", i);
				endpoint_in = ep_desc->bEndpointAddress;
			}
			else
			{
				printk(KERN_INFO "EP %d is Control OUT\n", i);
				endpoint_out = ep_desc->bEndpointAddress;
			}
		}
		if((epAttr & USB_ENDPOINT_XFERTYPE_MASK)==USB_ENDPOINT_XFER_INT)
		{
			if(epAddr & 0x80)
			{
				printk(KERN_INFO "EP %d is Interrupt IN\n", i);
				endpoint_in = ep_desc->bEndpointAddress;
			}
			else
			{
				printk(KERN_INFO "EP %d is Interrupt OUT\n", i);
				endpoint_out = ep_desc->bEndpointAddress;
			}
		}
		if((epAttr & USB_ENDPOINT_XFERTYPE_MASK)==USB_ENDPOINT_XFER_ISOC)
		{
			if(epAddr & 0x80)
			{
				printk(KERN_INFO "EP %d is Isochronous IN\n", i);
				endpoint_in = ep_desc->bEndpointAddress;
			}
			else
			{
				printk(KERN_INFO "EP %d is Isochronous OUT\n", i);
				endpoint_out = ep_desc->bEndpointAddress;
			}
		}		

	}

	
	printk(KERN_INFO "USB DEVICE CLASS : %x", interface->cur_altsetting->desc.bInterfaceClass);
	printk(KERN_INFO "USB DEVICE SUB CLASS : %x", interface->cur_altsetting->desc.bInterfaceSubClass);
	printk(KERN_INFO "USB DEVICE Protocol : %x", interface->cur_altsetting->desc.bInterfaceProtocol);


	int retval,retval1;  
	int r=0, r1=0,r2=0;  

	int size,size1;
	uint32_t max_lba, block_size;
	long device_size;
	static uint32_t tag = 1;
	uint32_t ret_tag;
	uint8_t cdb[16],cdb_len;;	// SCSI Command Descriptor Block

	uint8_t *lun=(uint8_t *)kmalloc(sizeof(uint8_t),GFP_KERNEL);
 
	struct command_block_wrapper *cbw;
	cbw=(struct command_block_wrapper *)kmalloc(sizeof(struct command_block_wrapper),GFP_KERNEL);

	uint8_t *buffer=(uint8_t *)kmalloc(64*sizeof(uint8_t),GFP_KERNEL);
	

	//reset
	printk("Reset mass storage device");
	r1 = usb_control_msg(device,usb_sndctrlpipe(device,0),BOMS_RESET,BOMS_RESET_REQ_TYPE,0,0,NULL,0,1000);
	if(r1<0)
		printk("error code: %d",r1);
	else
		printk("successful Reset");

	//reading lun
	printk(KERN_INFO "Reading Max LUN: %d\n",*lun);
	r = usb_control_msg(device,usb_sndctrlpipe(device,0), BOMS_GET_MAX_LUN ,BOMS_GET_MAX_LUN_REQ_TYPE,
	   0, 0, (void*)lun, 1, 1000);

	// Some devices send a STALL instead of the actual value.
	// In such cases we should set lun to 0.
	if (r == 0) {
		*lun = 0;
	} else if (r < 0) {
		printk(KERN_INFO "   Failed: %d\n", r);
	}
	printk(KERN_INFO "Max LUN = %d, r= %d\n", *lun,r);
    

	// Read capacity
	printk("Reading Capacity:\n");
	memset(buffer, 0, sizeof(buffer));
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = 0x25;	// Read Capacity

	cdb_len = cdb_length[cdb[0]];
	if ((cdb_len == 0) || (cdb_len > sizeof(cbw->CBWCB))) {
		printk("send_mass_storage_command: don't know how to handle this command (%02X, length %d)\n",
			cdb[0], cdb_len);
		return -1;
	}
	memset(cbw, 0, sizeof(*cbw));
	cbw->dCBWSignature[0] = 'U';
	cbw->dCBWSignature[1] = 'S';
	cbw->dCBWSignature[2] = 'B';
	cbw->dCBWSignature[3] = 'C';
	ret_tag = tag;
	cbw->dCBWTag = tag++;
	cbw->dCBWDataTransferLength = READ_CAPACITY_LENGTH;
	cbw->bmCBWFlags = endpoint_in;   // 1 for device to host
	cbw->bCBWLUN = *lun;
	// Subclass is 1 or 6 => cdb_len
	cbw->bCBWCBLength = cdb_len;
	memcpy(cbw->CBWCB, cdb, cdb_len);
	retval1 = usb_bulk_msg(device,usb_sndbulkpipe(device,endpoint_out),(void*)cbw, 31, &size1, 1000);     // sending cbw
	printk("sent %d CDB bytes\n", cdb_len);

	retval = usb_bulk_msg(device,usb_rcvbulkpipe(device,endpoint_in),(unsigned char*)buffer,READ_CAPACITY_LENGTH,&size, 1000);  //receiving data
	
	if(retval<0)
		printk(KERN_INFO "status of r2 %d",r2);
	printk(KERN_INFO "received %d bytes\n", size);
	max_lba = be_to_int32(&buffer[0]);
	block_size = be_to_int32(&buffer[4]);
	device_size = ((long)(max_lba+1))*block_size/(1024*1024);
	printk("DEVICE CAPACITY is %ld MB\n", device_size);
	
return 0;
}



/*Operations structure*/
static struct usb_driver usbdev_driver = {
	name: "usbdev",  //name of the device
	probe: usbdev_probe, // Whenever Device is plugged in
	disconnect: usbdev_disconnect, // When we remove a device
	id_table: usbdev_table, //  List of devices served by this driver
};


static int __init device_init(void)
{
	usb_register(&usbdev_driver);
	printk("UAS READ Capacity Driver Inserted\n");
	return 0;
}

static void __exit device_exit(void)
{
	usb_deregister(&usbdev_driver);
	printk(KERN_NOTICE "Leaving Kernel\n");
}

module_init(device_init);
module_exit(device_exit);
MODULE_LICENSE("GPL");
