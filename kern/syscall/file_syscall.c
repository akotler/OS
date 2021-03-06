#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/unistd.h>
#include <kern/stat.h>
#include <kern/seek.h>
#include <limits.h>
#include <spl.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <uio.h>
#include <synch.h>
#include <file_syscall.h>
#include <copyinout.h>
#include <vfs.h>
#include <vm.h>

/* Write to a user file */

int sys_open(const char *filename, int flags, int mode, int32_t *retval){
	
	int flag_val;
	int fd;
	int err;
	char *file_dest = (char*)kmalloc(sizeof(char)*PATH_MAX);
	
	size_t buflen;
	
	if (filename == NULL){
		*retval = -1;
		kfree(file_dest);
		return EFAULT;
	}
	
	//check if filesystem is full
	if(file_dest == NULL){
		*retval = -1;
		kfree(file_dest);
		return ENOSPC;
	}

	/* Are the flags within range? */
	if(flags < O_RDONLY || flags > O_NOCTTY){
		*retval = -1;
		kfree(file_dest);
		return EINVAL;
	}	

	flag_val = flags & O_ACCMODE;
		
	if(flag_val != O_RDONLY && flag_val != O_WRONLY && flag_val != O_RDWR){
		*retval = -1;
		kfree(file_dest);
		return EINVAL;
	}
	
	//copy userlevel filename to kernel level 
	err = copyinstr((const_userptr_t)filename, file_dest, PATH_MAX, &buflen);

	if(err){
		*retval = -1;
		kfree(file_dest);
		return err;
	}
	
	if(strlen(file_dest) == 0){
		*retval = -1;
		kfree(file_dest);
		return EINVAL;
	}
	
	//check whats in filetable after stdin, stdout, stderr
	for(fd = 3; fd < OPEN_MAX; fd++){
		if(curproc->file_table[fd] == NULL){
			break;
		}
	}

	/* Added the check to avoid reallocating for a reopened file
	 * Therefore, not kfreeing in sys_close
	 */
	if(curproc->file_table[fd] == NULL){
		curproc->file_table[fd] = (struct file_handle*)kmalloc(sizeof(struct file_handle*));
	}	

	//check if filesystem is full
	if(fd == OPEN_MAX || curproc->file_table[fd] == NULL){
		*retval = -1;
		kfree(file_dest);
		return ENFILE;
	}
	
	curproc->file_table[fd]->lock = lock_create(file_dest);
	lock_acquire(curproc->file_table[fd]->lock);

	//open file
	err = vfs_open(file_dest,flags,mode, &curproc->file_table[fd]->vnode);
	VOP_INCREF(curproc->file_table[fd]->vnode);

	if(err){
		*retval = -1;	
		kfree(file_dest);
		return EFAULT;
	}
	
	curproc->file_table[fd]->offset = 0;
	curproc->file_table[fd]->flags = flag_val;

	curproc->file_table[fd]->count++;
	lock_release(curproc->file_table[fd]->lock);
	
	*retval = fd;

	kfree(file_dest);
	return 0;
}

int sys_close(int fd, int32_t *retval){
	
	if(fd < 0 || fd >= OPEN_MAX){
		*retval = -1;
		return EBADF;
	}

	if(curproc->file_table[fd] == NULL){
		*retval = -1;
		return EBADF;
	}
	
	/* Can't close a file that's not open */
	if(curproc->file_table[fd]->vnode == NULL){
		*retval = -1;
		return EBADF;
	}
	
	vfs_close(curproc->file_table[fd]->vnode);
	if(curproc->file_table[fd]->vnode->vn_refcount == 0){
		vnode_cleanup(curproc->file_table[fd]->vnode);
	}	

	*retval = 0;
	return 0;
}

int sys_read(int fd, void *buf, size_t buflen, int32_t *retval){
	
        if(fd < 0 || fd >= OPEN_MAX){
                *retval = -1;
                return EBADF;
        }

        if(curproc->file_table[fd] == NULL){
                *retval = -1;
                return EBADF;
        }
		
	if(buf == NULL){
		*retval = -1;
		return EFAULT;
	}

	/* Not using a semaphore here because processes<->threads are 1 to 1 */
        if(curproc->file_table[fd]->lock == NULL){
                *retval = -1;
                return EBADF;
        }

        struct uio uio;
        struct iovec iovec;
        int err;

        lock_acquire(curproc->file_table[fd]->lock);

        if (curproc->file_table[fd]->vnode->vn_refcount == 0){
                *retval = -1;
                lock_release(curproc->file_table[fd]->lock);
                return EBADF;
        }

	if(curproc->file_table[fd]->flags == O_WRONLY){
		*retval = -1;
		lock_release(curproc->file_table[fd]->lock);
		return EBADF;
	}	


        uio_uinit(&iovec, &uio, buf, buflen, curproc->file_table[fd]->offset, UIO_READ);
        err = VOP_READ(curproc->file_table[fd]->vnode, &uio);


        if(err){
                *retval = -1;
                lock_release(curproc->file_table[fd]->lock);
                return err;
        }

        curproc->file_table[fd]->offset = uio.uio_offset;
        *retval = buflen - uio.uio_resid;
        lock_release(curproc->file_table[fd]->lock);
        return 0;

}

int sys_write(int fd, void *buf, size_t buflen, int32_t *retval){
	if(buf == NULL){
		*retval = -1;
		return EFAULT;
	}		

	if (fd < 0 || fd >= OPEN_MAX){
		*retval = -1;
		return EBADF;
	}
	
	if(curproc->file_table[fd] == NULL){
		*retval = -1;
		return EBADF;
	}

	if(curproc->file_table[fd]->lock == NULL){
		*retval = -1;
		return EBADF;
	}
	
	struct uio uio;
        struct iovec iovec;
	int err;

	lock_acquire(curproc->file_table[fd]->lock);

	if (curproc->file_table[fd]->vnode->vn_refcount == 0){
		*retval = -1;
		lock_release(curproc->file_table[fd]->lock);
		return EBADF;
	}
	
	if(curproc->file_table[fd]->flags == O_RDONLY){
		*retval = -1;
		lock_release(curproc->file_table[fd]->lock);
		return EBADF;
	}	

	uio_uinit(&iovec, &uio, buf, buflen, curproc->file_table[fd]->offset, UIO_WRITE);
	err = VOP_WRITE(curproc->file_table[fd]->vnode, &uio);
    
	
	if(err){
		*retval = -1;
		lock_release(curproc->file_table[fd]->lock);
		return EFAULT;
    	}

	curproc->file_table[fd]->offset = uio.uio_offset;
   	*retval = buflen - uio.uio_resid;
	lock_release(curproc->file_table[fd]->lock);
	return 0;
}

off_t sys_lseek(int fd, off_t pos, const_userptr_t whence, off_t *offset){
	if (fd < 0 || fd >= OPEN_MAX){
        	*offset = -1;
	        return EBADF;
	}

	if(curproc->file_table[fd] == NULL){
		*offset = -1;
		return EBADF;
	}

	if(curproc->file_table[fd]->lock == NULL){
		*offset = -1;
		return EBADF;
	}
	lock_acquire(curproc->file_table[fd]->lock);

    	int dest;

	if(curproc->file_table[fd]->vnode->vn_ops == (void *)0xdeadbeef){
		*offset = -1;
	        lock_release(curproc->file_table[fd]->lock);
        	return EBADF;
    	}

	copyin(whence, &dest, (size_t)sizeof(int));

	if(dest != SEEK_SET  && dest != SEEK_CUR  && dest != SEEK_END){
        	*offset = -1;
	        lock_release(curproc->file_table[fd]->lock);
        	return EINVAL;
    	}

	if(curproc->file_table[fd]->vnode == NULL){
	        *offset = -1;
	        lock_release(curproc->file_table[fd]->lock);
        	return EINVAL;
    	}

	if(!curproc->file_table[fd]->vnode->vn_refcount > 0){
		*offset = -1;
        	lock_release(curproc->file_table[fd]->lock);
	       	return EINVAL;
    	}

	struct stat stat;
    
	switch(dest){
        	case SEEK_SET:
	            curproc->file_table[fd]->offset = pos;
        	    break;

	        case SEEK_CUR:
	            curproc->file_table[fd]->offset += pos;
        	    break;

	        case SEEK_END:
        	{
	            VOP_STAT(curproc->file_table[fd]->vnode, &stat);
        	    curproc->file_table[fd]->offset  = stat.st_size + pos;
	            break;
        	}
		default:
		
       		    lock_release(curproc->file_table[fd]->lock);
		    return EINVAL;
			    
    	}

	bool err = VOP_ISSEEKABLE(curproc->file_table[fd]->vnode);
	    if (!err){
	    	*offset = -1;	
        	lock_release(curproc->file_table[fd]->lock);
        	return ESPIPE;
    	}	

    	*offset = curproc->file_table[fd]->offset;
    	lock_release(curproc->file_table[fd]->lock);
	if(*offset < 0){
		*offset = -1;
		return EINVAL;
	}

    	return 0;
}

int sys__getcwd(void *buf, size_t buflen, int32_t *retval){
	
	struct uio uio;
	struct iovec iovec;
	if(buf == NULL){
		*retval = -1;
		return EFAULT;
	}
//	-- Copies buf data into UIO --
	uio_uinit(&iovec, &uio, (userptr_t)buf, buflen, (off_t)0, UIO_READ);
	int err = vfs_getcwd(&uio);
	if (err) {
		*retval = -1;
		return EFAULT;
	}
	//Idea is right but not null terminating string 
//	-- Does this null terminate the string? --
	//buf[buflen] = '\0';
	
	*retval = buflen - uio.uio_resid;
	return 0;
}

int sys_dup2(int fd, int newfd, int32_t *retval){

	/* Checking that both file handles exist */
    	if(fd < STDIN_FILENO || fd >= OPEN_MAX){
                *retval = -1;
                return EBADF;
        }
	
	if (fd == newfd){
		*retval =-1;
		return EBADF;
	}
	
	if(newfd < 0 || newfd >= OPEN_MAX){
		*retval = -1;
		return EBADF;
	}

	if(curproc->file_table[fd] == NULL){
		*retval = -1;
		return EBADF;
	}
	
	if(fd == OPEN_MAX || curproc->file_table[fd] == NULL){
		*retval = -1;
		return EMFILE;
	}

	int err = 0;

	/* If this fd has never been used, allocate space for it */
	if(curproc->file_table[newfd] != NULL){
		err = sys_close(newfd, retval);
		if (err){
			*retval = -1;
			return EBADF;
                }
        }
	
	curproc->file_table[newfd] = curproc->file_table[fd];	

	curproc->file_table[newfd]->count++;
	*retval = newfd;
	return 0;
}

int sys_chdir(const char *pathname, int32_t *retval){
	if(pathname == NULL){
		*retval = -1;
		return EFAULT;	
	}

	int err;
	char *file_dest = (char*)kmalloc(sizeof(char)*PATH_MAX);	
	size_t buflen;
	
	copyinstr((const_userptr_t)pathname, file_dest, PATH_MAX, &buflen);
	
	if(strlen(file_dest) == 0){
		*retval = -1;
		kfree(file_dest);
		return EINVAL;
	}

	err = vfs_chdir(file_dest);
	
	if(err){
		*retval = -1;
		kfree(file_dest);
		return EFAULT;
	}

	*retval = 0;
	kfree(file_dest);
	return 0;
}
