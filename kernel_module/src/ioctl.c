//////////////////////////////////////////////////////////////////////
//                             University of California, Riverside
//
//
//
//                             Copyright 2020
//
////////////////////////////////////////////////////////////////////////
//
// This program is free software; you can redistribute it and/or modify it
// under the terms and conditions of the GNU General Public License,
// version 2, as published by the Free Software Foundation.
//
// This program is distributed in the hope it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
//
////////////////////////////////////////////////////////////////////////
//
//   Author: Abhishek Ayachit, Shashank Dahiya
//
//   Description:
//     Skeleton of NPHeap Pseudo Device
//
////////////////////////////////////////////////////////////////////////

#include "npheap.h"

#include <asm/processor.h>
#include <asm/segment.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/poll.h>
#include <linux/mutex.h>

extern struct miscdevice npheap_dev;
//=========================================================================================================================================

struct node_structure {
    struct node_structure* next;
    unsigned long size;
    void* data;
    unsigned long object_ID; 
    struct mutex lock;
};

DEFINE_MUTEX(list_lock);
struct node_structure* head;
struct node_structure* tail;

//=========================================================================================================================================

struct node_structure* find(unsigned long offset){
    struct node_structure* temp = head;
    while (temp != NULL && (temp -> object_ID) != offset)	
        temp = temp -> next;
   
    return temp;
}


struct node_structure* create_node(unsigned long object_ID){
    if (head == NULL){//empty

        head = (struct node_structure*) kmalloc(sizeof(struct node_structure), GFP_KERNEL);
        head->next = NULL;
        head->data = NULL;
        head->size = 0;
        head->object_ID = object_ID;
        mutex_init(&(head->lock));
        tail = head;
        return head;
    }
    else{// non-empty
        struct node_structure* new_node = (struct node_structure*) kmalloc(sizeof(struct node_structure), GFP_KERNEL);
        tail->next = new_node;
        tail = new_node;
        new_node->next = NULL;
        new_node->data = NULL;
        new_node->size = 0;
        new_node->object_ID = object_ID;
        mutex_init(&(new_node->lock));
        return new_node;
    }
}


//=========================================================================================================================================

int npheap_mmap(struct file *filp, struct vm_area_struct *vma)
{
    unsigned long node_size = (vma->vm_end) - (vma->vm_start);
    mutex_lock(&list_lock);
    struct node_structure* node = find(vma->vm_pgoff);
    if (!node){//did not find; return
        mutex_unlock(&list_lock);
        return EPERM;
    } 
    mutex_unlock(&list_lock);
    void* allocated_data;
    if (node->data == NULL){//data= null; allocate data
        allocated_data = kmalloc(node_size, GFP_KERNEL);
        if (allocated_data == NULL)
            return ENOMEM;
        if (node->size != 0){
            kfree(allocated_data);
            return ENOMEM;
        }
        node->size = node_size;
        node->data = allocated_data;
    }
    else{	//data!=null 
    	if (node_size != node->size) //check size  ///////////////////////////////////////////////////////
            return EINVAL;
        allocated_data = node->data;	//if same, allocate data
    }
    if (remap_pfn_range(vma, vma->vm_start, virt_to_phys(allocated_data) >> PAGE_SHIFT, node_size, vma->vm_page_prot)){
        kfree(allocated_data);
        node->size = 0;
        node->data = NULL;
        return EAGAIN;
    }
    return 0;
}


//=========================================================================================================================================

int npheap_init(void)
{
    int ret;
    if ((ret = misc_register(&npheap_dev)))
        printk(KERN_ERR "Unable to register \"npheap\" misc device\n");
    else
        printk(KERN_ERR "\"npheap\" misc device installed\n");
    return ret;
}


//=========================================================================================================================================

void npheap_exit(void)
{
    misc_deregister(&npheap_dev);
}

//=========================================================================================================================================

// If exist, return the data.
long npheap_lock(struct npheap_cmd __user *user_cmd)
{
    struct npheap_cmd kern_command;
    if (copy_from_user(&kern_command, user_cmd, sizeof(struct npheap_cmd))){
        return EINVAL; 
    }
    mutex_lock(&list_lock);
    struct node_structure* object = find(kern_command.offset / PAGE_SIZE);
    if (object != NULL){   
        mutex_unlock(&list_lock);
        mutex_lock(&(object->lock));
        return 0;
    }
    else {
        object = create_node(kern_command.offset / PAGE_SIZE);
        if (object == NULL){
            mutex_unlock(&list_lock);
            return ENOMEM;
        }
    }
    mutex_unlock(&list_lock);
    mutex_lock(&(object->lock));
    return 0;
}     


//=========================================================================================================================================

long npheap_unlock(struct npheap_cmd __user *user_cmd)
{
    struct npheap_cmd kern_command;
    if (copy_from_user(&kern_command, user_cmd, sizeof(struct npheap_cmd))){
        return EINVAL; 
    }
    mutex_lock(&list_lock);
    struct node_structure* object = find(kern_command.offset / PAGE_SIZE);
    if (object){
        mutex_unlock(&list_lock);
        mutex_unlock(&(object->lock));
        return 0;
    }
    mutex_unlock(&list_lock);
    return EPERM; 
}

//=========================================================================================================================================

long npheap_getsize(struct npheap_cmd __user *user_cmd)
{   
    struct npheap_cmd kern_command;
    if (copy_from_user(&kern_command, user_cmd, sizeof(struct npheap_cmd)))
        return EINVAL; 
    struct node_structure* object = find(kern_command.offset / PAGE_SIZE);
    if (object)
        return object->size;

    return EINVAL; 
}

//=========================================================================================================================================

long npheap_delete(struct npheap_cmd __user *user_cmd)
{
    struct npheap_cmd kern_command;
    if (copy_from_user(&kern_command, user_cmd, sizeof(struct npheap_cmd)))
        return EINVAL; 

    mutex_lock(&list_lock);
    struct node_structure* object = find(kern_command.offset / PAGE_SIZE);
    if (object && object->data){
        mutex_unlock(&list_lock);
       
       //delete node
        kfree(object->data);
        object->data = NULL;
        object->size = 0;
    	
    }
    else{
        mutex_unlock(&list_lock);
        return EPERM; 
    }
    return 0;
}

//=========================================================================================================================================

long npheap_ioctl(struct file *filp, unsigned int cmd,
                                unsigned long arg)
{
    switch (cmd) {
    case NPHEAP_IOCTL_LOCK:
        return npheap_lock((void __user *) arg);
    case NPHEAP_IOCTL_UNLOCK:
        return npheap_unlock((void __user *) arg);
    case NPHEAP_IOCTL_GETSIZE:
        return npheap_getsize((void __user *) arg);
    case NPHEAP_IOCTL_DELETE:
        return npheap_delete((void __user *) arg);
    default:
        return -ENOTTY;
    }
}

//=========================================================================================================================================