#include<linux/module.h>
#include<linux/fs.h>
#include<linux/init.h>
#include<linux/cdev.h>
#include<linux/slab.h>
#include<linux/uaccess.h>

#define  GLOBALMEM_SIZE   0x1000//字符驱动分配一个4KB的内存空间，在驱动中针对这片内存进行读写控制定位操作
#define  MEM_CLEAR        0x1
#define  GLOBALMEM_MAJOR 230//主设备号
#define DEVICE_NUM       10

static  int  globalmem_major = GLOBALMEM_MAJOR;
module_param(globalmem_major, int, S_IRUGO);//传参给内核空间

struct  globalmem_dev{
	struct cdev  cdev;//globalmem字符设备的cdev
	unsigned  char  mem[GLOBALMEM_SIZE];//内存，一个数组
};//封装的思想

struct   globalmem_dev  *globalmem_devp;

static int globalmem_open(struct inode *inode, struct file *filp)
{
//	filp->private_data = globalmem_devp;
	/*当inode结点指向一个字符设备文件时，i_cdev位指向inode结构的一个指针*/
	/*container_of通过结构体成员的指针找到对应节点的结构体指针*/
	/*根据结构体成员获得这个结构体的首地址*/
	struct  globalmem_dev *dev = container_of(inode->i_cdev,struct globalmem_dev, cdev);
	filp->private_data = dev;
	return 0;
}

static int globalmem_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static long globalmem_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct globalmem_dev *dev = filp->private_data;

	switch (cmd){
	case MEM_CLEAR:
		memset(dev->mem, 0, GLOBALMEM_SIZE);
		printk(KERN_INFO "globalmem is set to zero\n");
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static ssize_t globalmem_read(struct file *filp, char __user *buf, size_t size, loff_t *ppos)
{
	unsigned long p = *ppos;
	unsigned int count = size;
	int ret = 0;
	struct globalmem_dev *dev = filp->private_data;
	/*文件指针溢出直接返回，说明读到了末尾*/
	if(p >= GLOBALMEM_SIZE)
		return 0;
	/*如果所要读出的内容大于globalmem当前内存指针到内存末尾的大小，就把当前指针后面的所有读出来*/
	if(count > GLOBALMEM_SIZE - p)
		count = GLOBALMEM_SIZE - p;
	/*由于用户空间不能直接操作内核空间的内存，copy_to_user完成内核空间到用户空间缓冲区的复制*/
	/*copy_to_user如果成功复制，返回值是0，如果复制失败，返回值是还有多少bytes没有完成复制*/
	if(copy_to_user(buf, dev->mem + p, count)){
		ret = -EFAULT;
	}else{
		*ppos +=count;			
		ret = count;

		printk(KERN_INFO "read %u bytes(s) from %lu ", count, p);
	}	
	/*返回值：成功返回读取的字节数，失败返回EFAULT*/
	return ret;
}

static ssize_t globalmem_write(struct file *filp, const char __user *buf, size_t size, loff_t *ppos)
{
	unsigned long p = *ppos;
	unsigned int count = size;
	int ret = 0;
	struct globalmem_dev *dev = filp->private_data;

	if(p >= GLOBALMEM_SIZE){
		return 0;
	} 
	if(count > GLOBALMEM_SIZE - p)
		count = GLOBALMEM_SIZE - p;

	if(copy_from_user(dev->mem + p, buf, count)){
		ret = -EFAULT;
	}else{
		*ppos  += count;
		ret = count;

		printk(KERN_INFO "writen %u byte(s) from %lu", count, p);
	}
	return ret;

}

static loff_t globalmem_llseek(struct file *filp , loff_t offset ,int orig)
{
	loff_t ret = 0;
	switch(orig){
	case 0:
		if(offset < 0){
			ret = -EINVAL;
			break;
		}
		if((unsigned int)offset > GLOBALMEM_SIZE){
			ret = -	EINVAL;
			break;
		}
		filp->f_pos = (unsigned int )offset;
		ret = filp->f_pos;
		break;
	case 1:
		if((filp->f_pos + offset) > GLOBALMEM_SIZE){
			ret = -EINVAL;
			break;
		}
		if((filp->f_pos + offset ) < 0){
			ret = -EINVAL;
			break;
		}
		filp->f_pos += offset;
		ret = filp->f_pos;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static const struct file_operations globalmem_fops = {
	.owner = THIS_MODULE,
	.llseek = globalmem_llseek,
	.read = globalmem_read,
	.write = globalmem_write,
	.unlocked_ioctl = globalmem_ioctl,
	.open = globalmem_open,
	.release = globalmem_release,
};

static  void  globalmem_setup_cdev(struct  globalmem_dev  *dev, int  index)
{
	int  err,devno = MKDEV(globalmem_major, index);//globalmem_dev主设备号(高12位)index从设备号(低20位)
	//devno是合并过的设备号，err用于存储cdev_add的返回值
	cdev_init(&dev->cdev, &globalmem_fops);//初始化cdev结构体，并且关联globalmem_fops结构体
	dev->cdev.owner = THIS_MODULE;//设置所属模块
	err = cdev_add(&dev->cdev, devno, 1);//注册成功返回值是0,第三个参数该类设备的设备个数
	if(err){
		printk(KERN_NOTICE"Error %d adding globermem%d",err, index);
	}	
}

/*全局内存（设备）初始化*/
static   int  __init  globalmem_init(void)
{
	int  ret;
	int i;
	dev_t  devno  =  MKDEV(globalmem_major, 0);

	if(globalmem_major){
	//	ret  =  register_chrdev_region(devno, 1, "globalmem");//静态分配设备号
		ret = register_chrdev_region(devno, DEVICE_NUM,"globalmem");
	}else{
      	//	ret  =  alloc_chrdev_region(&devno, 0, 1, "globalmem");//动态分配设备号
		ret = alloc_chrdev_region(&devno, 0, DEVICE_NUM, "globalmem");
	globalmem_major = MAJOR(devno);//提取高12位主设备号
	}	
	/*设备号分配失败会返回负值*/
	if(ret < 0)
		return ret;

	globalmem_devp = kzalloc(sizeof(struct  globalmem_dev), GFP_KERNEL);//向内核空间申请内存给globalmem_dev，可以添加_GFP_ZERO清0
	/*申请失败的处理*/
	if(!globalmem_devp){
	ret  = -ENOMEM;
	goto fail_malloc;
	}
	for(i = 0; i< DEVICE_NUM; i++)
		globalmem_setup_cdev(globalmem_devp + i, i);
//	globalmem_setup_cdev(globalmem_devp, 0);
	return  0;
	
	fail_malloc:
	unregister_chrdev_region(devno, DEVICE_NUM);	
//	fail_malloc:
//	unregister_chrdev_region(devno, 1);
	return  ret;
}
module_init(globalmem_init);

static void __exit globalmem_exit(void)
{
	int i;
	for(i = 0; i < DEVICE_NUM; i++)
		cdev_del(&(globalmem_devp + i)->cdev);
//	cdev_del(&globalmem_devp->cdev);
	kfree(globalmem_devp);
	unregister_chrdev_region(MKDEV(globalmem_major,0), DEVICE_NUM);
//	unregister_chrdev_region(MKDEV(globalmem_major,0), 1);
}
module_exit(globalmem_exit);

MODULE_AUTHOR("XUWEIJIE");
MODULE_LICENSE("GPL v2");
















