#ifndef _SCULL_H_
#define _SCULL_H_


#include <linux/module.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/mutex.h>

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Arthur");
MODULE_DESCRIPTION("scull");

#ifndef SCULL_MAJOR
#define SCULL_MAJOR 0 /* dynamic major */
#endif

#ifndef SCULL_MINOR
#define SCULL_MINOR 0 /* dynamic minor (I think) */
#endif

#ifndef SCULL_NR_DEVS
#define SCULL_NR_DEVS 4 /* scull[0-4] */
#endif

#ifndef SCULL_P_NR_DEVS
#define SCULL_P_NR_DEVS 4 /* scullpipe[0-4] */
#endif

/* Macros to control the layout of memory managed by scull */

#ifndef SCULL_QUANTUM
#define SCULL_QUANTUM 4000
#endif

#ifndef SCULL_QSET
#define SCULL_QSET    1000
#endif

struct scull_dev *scull_devices;        /* allocated in scull_init_module */

/* 
 * file operations associated with a device managed by this driver
*/
loff_t scull_llseek(struct file*, loff_t, int);
ssize_t scull_read(struct file*, char __user *, size_t, loff_t *);
ssize_t scull_write(struct file*, const char __user *, size_t, loff_t *);
long scull_ioctl(struct file *, unsigned int, unsigned long);
int scull_open(struct inode *, struct file *);
int scull_release(struct inode *, struct file *);

struct file_operations scull_fops = {
    .owner = THIS_MODULE,
    .llseek = scull_llseek,
    .read = scull_read,
    .write = scull_write,
    .unlocked_ioctl = scull_ioctl,
    .open = scull_open,
    .release = scull_release,
};

struct scull_qset {
    void **data;
    struct scull_qset *next;
};

struct scull_dev {
    struct scull_qset *data;
    int quantum;
    int qset;
    unsigned long size;
    unsigned int access_key;
    struct mutex mutex;
    struct cdev cdev;
};

/*
 * initialize the char device owned by the supplied scull_dev
*/
static void scull_setup_cdev(struct scull_dev *dev, int index);

#endif // _SCULL_H_
