TUSHAR ANIL PATIL     2019H140096G

JAGIRAPU NIKHIL REDDY 209H1400544G

.>>>>>>>>>USB BLOCK DRIVER CODE FOR READING AND WRITING FILES IN USB USING SCSI COMMANDS <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< 

step 1: Go to the directory and give the command  $ make all

step 2: connect pendrive and remove it. remove kernel drivers using commands   $ sudo rmmod uas 
                                               				       $ sudo rmmod usb_storage 

step 3: insert the module using  $ sudo insmod main.ko

step 4: check whether module is loaded using command  $ lsmod 

step 5: connect pendrive

step 6: make folder inside media directory  $ sudo mkdir /media/nusb

step 7: use mount command for accessing pendrive  $ sudo mount -t vfat /dev/mydisk1 /media/nusb

step 8: go inside root  $ sudo -i

step 9: go inside pusb folder of media directory  $ cd /media/nusb/

step 10: command to see content of pendrive  $ ls

step 11: command to create new text file and writing HelloWorld into it  $ echo HelloWorld>test.txt

step 12: command to read content of file  $ cat test.txt

step 13: command to come outside of root  $ logout

step 14: remove pendrive and again connect ,check its content you can clearly see new file test.txt in it along with already existing files

