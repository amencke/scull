#include "scull.h"

int scull_major =   SCULL_MAJOR;
int scull_minor =   0;
int scull_nr_devs = SCULL_NR_DEVS;	/* number of bare scull devices */
int scull_quantum = SCULL_QUANTUM;
int scull_qset =    SCULL_QSET;

module_param(scull_major, int, S_IRUGO);
module_param(scull_minor, int, S_IRUGO);
module_param(scull_nr_devs, int, S_IRUGO);
module_param(scull_quantum, int, S_IRUGO);
module_param(scull_qset, int, S_IRUGO);

static int scull_trim(struct scull_dev *dev)
{
    struct scull_qset *next, *dptr;
    int qset = dev->qset; /* "dev" is not-null */
    int i;
    for (dptr = dev->data; dptr; dptr = next) { /* all the list items */
	if (dptr->data) {
	    for (i = 0; i < qset; i++)
		kfree(dptr->data[i]);
	    kfree(dptr->data);
	    dptr->data = NULL;
	}
	next = dptr->next;
	kfree(dptr);
    }
    dev->size = 0;
    dev->quantum = scull_quantum;
    dev->qset = scull_qset;
    dev->data = NULL;
    return 0;
}

struct scull_qset *scull_follow(struct scull_dev *dev, int n)
{
    struct scull_qset *qs = dev->data;

    /* Allocate first qset explicitly if need be */
    if (! qs) {
	qs = dev->data = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
	if (qs == NULL)
	    return NULL;  /* Never mind */
	memset(qs, 0, sizeof(struct scull_qset));
    }

    /* Then follow the list */
    while (n--) {
	if (!qs->next) {
	    qs->next = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
	    if (qs->next == NULL)
		return NULL;  /* Never mind */
	    memset(qs->next, 0, sizeof(struct scull_qset));
	}
	qs = qs->next;
	continue;
    }
    return qs;
}


loff_t scull_llseek(struct file* filp, loff_t offset, int whence)
{
    return 0;
}

ssize_t scull_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    struct scull_dev *dev = filp->private_data; 
    struct scull_qset *dptr;	/* the first listitem */
    int quantum = dev->quantum, qset = dev->qset;
    int itemsize = quantum * qset; /* how many bytes in the listitem */
    int item, s_pos, q_pos, rest;
    ssize_t retval = 0;

    if (mutex_lock_interruptible(&dev->mutex))
	return -ERESTARTSYS;
    if (*f_pos >= dev->size)
	goto out;
    if (*f_pos + count > dev->size)
	count = dev->size - *f_pos;

    /* find listitem, qset index, and offset in the quantum */
    item = (long)*f_pos / itemsize;
    rest = (long)*f_pos % itemsize;
    s_pos = rest / quantum; q_pos = rest % quantum;

    /* follow the list up to the right position (defined elsewhere) */
    dptr = scull_follow(dev, item);

    if (dptr == NULL || !dptr->data || ! dptr->data[s_pos])
	goto out; /* don't fill holes */

    /* read only up to the end of this quantum */
    if (count > quantum - q_pos)
	count = quantum - q_pos;

    if (copy_to_user(buf, dptr->data[s_pos] + q_pos, count)) {
	retval = -EFAULT;
	goto out;
    }
    *f_pos += count;
    retval = count;

out:
    mutex_unlock(&dev->mutex);
    return retval;
}

ssize_t scull_write(struct file *filp, const char __user *buf, size_t count,
	loff_t *f_pos)
{
    struct scull_dev *dev = filp->private_data;
    struct scull_qset *dptr;
    int quantum = dev->quantum, qset = dev->qset;
    int itemsize = quantum * qset;
    int item, s_pos, q_pos, rest;
    ssize_t retval = -ENOMEM; /* value used in "goto out" statements */

    if (mutex_lock_interruptible(&dev->mutex))
	return -ERESTARTSYS;

    /* find listitem, qset index and offset in the quantum */
    item = (long)*f_pos / itemsize;
    rest = (long)*f_pos % itemsize;
    s_pos = rest / quantum; q_pos = rest % quantum;

    /* follow the list up to the right position */
    dptr = scull_follow(dev, item);
    if (dptr == NULL)
	goto out;
    if (!dptr->data) {
	dptr->data = kmalloc(qset * sizeof(char *), GFP_KERNEL);
	if (!dptr->data)
	    goto out;
	memset(dptr->data, 0, qset * sizeof(char *));
    }
    if (!dptr->data[s_pos]) {
	dptr->data[s_pos] = kmalloc(quantum, GFP_KERNEL);
	if (!dptr->data[s_pos])
	    goto out;
    }
    /* write only up to the end of this quantum */
    if (count > quantum - q_pos)
	count = quantum - q_pos;

    if (copy_from_user(dptr->data[s_pos]+q_pos, buf, count)) {
	retval = -EFAULT;
	goto out;
    }
    *f_pos += count;
    retval = count;

    /* update the size */
    if (dev->size < *f_pos)
	dev->size = *f_pos;

out:
    mutex_unlock(&dev->mutex);
    return retval;
}

long scull_ioctl(struct file *filp, unsigned int change1, unsigned long change2)
{
    return 0;
}

int scull_open(struct inode *inode, struct file *filp)
{
    struct scull_dev *dev;

    dev = container_of(inode->i_cdev, struct scull_dev, cdev);
    filp->private_data = dev;

    /* trim to size 0 if the device was opened write-only */
    if ( (filp->f_flags & O_ACCMODE) == O_WRONLY) {
	scull_trim(dev);
    }
    return 0;
}

int scull_release(struct inode *inode, struct file *filp)
{
    return 0;
}

/* module init and exit functions */

void scull_cleanup_module(void)
{
    int i;
    dev_t devno = MKDEV(scull_major, scull_minor);

    /* remove the character devices from the system */
    if (scull_devices)
    {
	for (i = 0; i < scull_nr_devs; ++i)
	{
	    scull_trim(scull_devices + i);
	    cdev_del(&scull_devices[i].cdev);
	}
	kfree(scull_devices);
    }

    unregister_chrdev_region(devno, scull_nr_devs);
}

static void scull_setup_cdev(struct scull_dev *dev, int index)
{
    int err, devno = MKDEV(scull_major, scull_minor + index);

    cdev_init(&dev->cdev, &scull_fops);
    dev->cdev.owner = THIS_MODULE;
    err = cdev_add(&dev->cdev, devno, 1);

    if (err)
        printk(KERN_NOTICE "Error %d adding scull%d", err, index);

    printk(KERN_INFO "Added scull%d, device %d\n", index, MINOR(dev->cdev.dev));
}


int scull_init_module(void)
{
    int result, i;
    dev_t dev;

    /* use dynamic major unless one is provided at module load time */
    if (scull_major)
    {
	dev = MKDEV(scull_major, scull_minor);
	result = register_chrdev_region(dev, scull_nr_devs, "scull");
    } else {
	result = alloc_chrdev_region(&dev, scull_minor, scull_nr_devs,
		"scull");
	scull_major = MAJOR(dev);
    }

    if (result < 0) {
	printk(KERN_WARNING "scull: can't get major %d\n", scull_major);
	return result;
    }

    /* allocate the devices */
    scull_devices = kmalloc(scull_nr_devs * sizeof(struct scull_dev), GFP_KERNEL);
    if (!scull_devices) {
	result = -ENOMEM;
	goto fail;
    }
    memset(scull_devices, 0, scull_nr_devs * sizeof(struct scull_dev));

    /* initialize each device */
    for (i = 0; i < scull_nr_devs; ++i) {
	scull_devices[i].quantum = scull_quantum;
	scull_devices[i].qset = scull_qset;
	mutex_init(&scull_devices[i].mutex);
	scull_setup_cdev(&scull_devices[i], i);
    }

    return 0; /* succeed */

fail:
    scull_cleanup_module();
    printk(KERN_ERR "Failed to init scull module, exiting");
    return result;
}

module_init(scull_init_module);
module_exit(scull_cleanup_module);
