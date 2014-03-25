#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/mm_types.h>
#include <linux/mm.h>
#include <linux/ktime.h>
#include <linux/string.h>
#include "getvminfo.h"

struct task_struct *call_task = NULL;
char *respbuf;

int file_value;
struct dentry *dir, *file;

//The following two variables substitute the two of the three possible vm_ops.
struct vm_operations_struct my_vm_ops1;
struct vm_operations_struct my_vm_ops2;

//Similar for the following two functions
int (*my_fault1) (struct vm_area_struct *vma, struct vm_fault *vmf);
int (*my_fault2) (struct vm_area_struct *vma, struct vm_fault *vmf);

/*The first fault function with logging*/
int log_fault1(struct vm_area_struct *vma, struct vm_fault *vmf)
{
  ktime_t start, end;
  s64 actual_time;
  int result;
  start = ktime_get();
  result = my_fault1(vma, vmf);
  end = ktime_get();
  actual_time = ktime_to_ns(ktime_sub(end, start));
  printk("1 %lu %lu %lu %lu %lld\n", (unsigned long) vma -> vm_mm, (unsigned long) vmf -> virtual_address, vmf -> pgoff, page_to_pfn(vmf -> page), (long long)actual_time);
  return result;
}

int log_fault2(struct vm_area_struct *vma, struct vm_fault *vmf)
{
  ktime_t start, end;
  s64 actual_time;
  int result;
  start = ktime_get();
  result = my_fault2(vma, vmf);
  end = ktime_get();
  actual_time = ktime_to_ns(ktime_sub(end, start));
  printk("2 %lu %lu %lu %lu %lld\n", (unsigned long) vma -> vm_mm, (unsigned long) vmf -> virtual_address, vmf -> pgoff, page_to_pfn(vmf -> page), (long long)actual_time);
  return result;
}

static ssize_t getvminfo_call(struct file *file, const char __user *buf,
                                size_t count, loff_t *ppos)
{
  int rc;
  char *fullname;
  char *command_name;
  char *sflag;
  char callbuf[MAX_CALL];
  char resp_line[MAX_LINE];
  struct vm_area_struct *it;
  int flag = 0;
  int i;
  int vma_count;
  int sem_ret = 0;
  long temp = 0;
  unsigned long addr1 = 0;
  unsigned long addr2 = 0;
  //struct vm_operations_struct my_vm_ops;
  /* the user's write() call should not include a count that exceeds
   * the size of the module's buffer for the call string.
   */
  if(count >= MAX_CALL)
     return -EINVAL;

  /* The preempt_disable() and preempt_enable() functions are used in the
   * kernel for preventing preemption.  They are used here to protect
   * global state.
   */
  
  preempt_disable();

  if (call_task != NULL) {
     preempt_enable(); 
     return -EAGAIN;
  }

  respbuf = kmalloc(MAX_RESP, GFP_ATOMIC);
  if (respbuf == NULL) {
     preempt_enable(); 
     return -ENOSPC;
  }
  strcpy(respbuf,""); /* initialize buffer with null string */

  /* current is global for the kernel and contains a pointer to the
   * running process
   */
  call_task = current;

  /* Use the kernel function to copy from user space to kernel space.
   */

  rc = copy_from_user(callbuf, buf, count);
  callbuf[MAX_CALL - 1] = '\0'; /* make sure it is a valid string */
  fullname = kmalloc(MAX_CALL, GFP_ATOMIC);
  strcpy(fullname, callbuf);
  command_name = strsep(&fullname, " ");
  
  if (strcmp(command_name, "getvminfo") != 0) {
      strcpy(respbuf, "Failed: invalid operation\n");
      printk(KERN_DEBUG "getvminfo: call %s will return %s\n", callbuf, respbuf);
      preempt_enable();
      return count;  /* write() calls return the number of bytes written */
  }

  sflag = strsep(&fullname, " ");
  if(!kstrtol(sflag, 10, &temp)){
    flag = (int) temp;
  }

 /*
  *Here we record the information we need to the respbuf and modify the
  *set of pointers in vm_area_struct
  */
  
  sem_ret = 0;
  while(sem_ret == 0){
    sem_ret = down_read_trylock(&call_task -> active_mm -> mmap_sem);
  }
  //So this is the root/head of the list of VMAs allocate!
  it = call_task -> active_mm -> mmap;
  vma_count = call_task -> active_mm -> map_count;
  sprintf(respbuf, "VMAs:\n");
  for(i = 0; i < vma_count; i++)
  {
    sprintf(resp_line, "%d. start: %lu end: %lu flag: %lu vm_ops: %lu\n", i+1, it -> vm_start, it -> vm_end, it -> vm_flags, (unsigned long)it -> vm_ops);
    if((unsigned long) it -> vm_ops != 0){
      //If addr1 is not given the right value
      if(addr1==0){
        addr1 = (unsigned long) it -> vm_ops;
        my_fault1 = it -> vm_ops -> fault;
        my_vm_ops1.open = it -> vm_ops -> open;
        my_vm_ops1.close = it -> vm_ops -> close;
        my_vm_ops1.page_mkwrite = it -> vm_ops -> page_mkwrite;
        my_vm_ops1.access = it -> vm_ops -> access;
        my_vm_ops1.fault = &log_fault1;
        
      //If addr2 is not given the right value
      //And the currnt vm_ops != addr1
      //We have the right value for addr2
      }else if(addr2==0 && (unsigned long) it -> vm_ops != addr1){
        addr2 = (unsigned long) it -> vm_ops;
        my_fault2 = it -> vm_ops -> fault;
        my_vm_ops2.open = it -> vm_ops -> open;
        my_vm_ops2.close = it -> vm_ops -> close;
        my_vm_ops2.page_mkwrite = it -> vm_ops -> page_mkwrite;
        my_vm_ops2.access = it -> vm_ops -> access;
        my_vm_ops2.fault = &log_fault2;
        
      }
    }
    strcat(respbuf, resp_line);
    it = it -> vm_next;
  }  
  if(flag != 0){
  it = call_task -> active_mm -> mmap;
  for(i = 0; i < vma_count; i++)
  {
    if((unsigned long) it -> vm_ops!=0){
      if((unsigned long) it -> vm_ops == addr1){
        it -> vm_ops = &my_vm_ops1;
      }else if((unsigned long) it -> vm_ops == addr2){
        it -> vm_ops = &my_vm_ops2;
      }
    }
    it = it -> vm_next;
  }
  }
  up_read(&call_task -> active_mm -> mmap_sem);
  /* Here the response has been generated and is ready for the user
   * program to access it by a read() call.
   */
  
  printk(KERN_DEBUG "getvminfo: call %s will return %s", callbuf, respbuf);
  preempt_enable();
  
  *ppos = 0;  /* reset the offset to zero */
  return count;  /* write() calls return the number of bytes written */
}

/* This function emulates the return from a system call by returning
 * the response to the user.
 */

static ssize_t getvminfo_return(struct file *file, char __user *userbuf,
                                size_t count, loff_t *ppos)
{
  int rc; 

  preempt_disable();

  if (current != call_task) {
     preempt_enable();
     return 0;
  }

  rc = strlen(respbuf) + 1; /* length includes string termination */

  /* return at most the user specified length with a string 
   * termination as the last byte.  Use the kernel function to copy
   * from kernel space to user space.
   */

  if (count < rc) {
     respbuf[count - 1] = '\0';
     rc = copy_to_user(userbuf, respbuf, count);
  }
  else 
     rc = copy_to_user(userbuf, respbuf, rc);

  kfree(respbuf);

  respbuf = NULL;
  call_task = NULL;

  preempt_enable();

  *ppos = 0;  /* reset the offset to zero */
  return rc;  /* read() calls return the number of bytes read */
} 

static const struct file_operations my_fops = {
        .read = getvminfo_return,
        .write = getvminfo_call,
};

/* This function is called when the module is loaded into the kernel
 * with insmod.  It creates the directory and file in the debugfs
 * file system that will be used for communication between programs
 * in user space and the kernel module.
 */

static int __init getvminfo_module_init(void)
{

  /* create a directory to hold the file */

  dir = debugfs_create_dir(dir_name, NULL);
  if (dir == NULL) {
    printk(KERN_DEBUG "getvminfo: error creating %s directory\n", dir_name);
     return -ENODEV;
  }

  /* create the in-memory file used for communication;
   * make the permission read+write by "world"
   */


  file = debugfs_create_file(file_name, 0666, dir, &file_value, &my_fops);
  if (file == NULL) {
    printk(KERN_DEBUG "getvminfo: error creating %s file\n", file_name);
     return -ENODEV;
  }

  printk(KERN_DEBUG "getvminfo: created new debugfs directory and file\n");

  return 0;
}

/* This function is called when the module is removed from the kernel
 * with rmmod.  It cleans up by deleting the directory and file and
 * freeing any memory still allocated.
 */

static void __exit getvminfo_module_exit(void)
{
  debugfs_remove(file);
  debugfs_remove(dir);
  if (respbuf != NULL)
     kfree(respbuf);
}

/* Declarations required in building a module */

module_init(getvminfo_module_init);
module_exit(getvminfo_module_exit);
MODULE_LICENSE("GPL");
