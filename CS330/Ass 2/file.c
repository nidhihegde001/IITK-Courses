#include<types.h>
#include<context.h>
#include<file.h>
#include<lib.h>
#include<serial.h>
#include<entry.h>
#include<memory.h>
#include<fs.h>
#include<kbd.h>


/************************************************************************************/
/***************************Do Not Modify below Functions****************************/
/************************************************************************************/

void free_file_object(struct file *filep)
{
	if(filep)
	{
		os_page_free(OS_DS_REG ,filep);
		stats->file_objects--;
	}
}

struct file *alloc_file()
{
	struct file *file = (struct file *) os_page_alloc(OS_DS_REG); 
	file->fops = (struct fileops *) (file + sizeof(struct file)); 
	bzero((char *)file->fops, sizeof(struct fileops));
	file->ref_count = 1;
	file->offp = 0;
	stats->file_objects++;
	return file; 
}

void *alloc_memory_buffer()
{
	return os_page_alloc(OS_DS_REG); 
}

void free_memory_buffer(void *ptr)
{
	os_page_free(OS_DS_REG, ptr);
}

/* STDIN,STDOUT and STDERR Handlers */

/* read call corresponding to stdin */

static int do_read_kbd(struct file* filep, char * buff, u32 count)
{
	kbd_read(buff);
	return 1;
}

/* write call corresponding to stdout */

static int do_write_console(struct file* filep, char * buff, u32 count)
{
	struct exec_context *current = get_current_ctx();
	return do_write(current, (u64)buff, (u64)count);
}

long std_close(struct file *filep)
{
	filep->ref_count--;
	if(!filep->ref_count)
		free_file_object(filep);
	return 0;
}
struct file *create_standard_IO(int type)
{
	struct file *filep = alloc_file();
	filep->type = type;
	if(type == STDIN)
		filep->mode = O_READ;
	else
		filep->mode = O_WRITE;
	if(type == STDIN){
		filep->fops->read = do_read_kbd;
	}else{
		filep->fops->write = do_write_console;
	}
	filep->fops->close = std_close;
	return filep;
}

int open_standard_IO(struct exec_context *ctx, int type)
{
	int fd = type;
	struct file *filep = ctx->files[type];
	if(!filep){
		filep = create_standard_IO(type);
	}else{
		filep->ref_count++;
		fd = 3;
		while(ctx->files[fd])
			fd++; 
	}
	ctx->files[fd] = filep;
	return fd;
}
/**********************************************************************************/
/**********************************************************************************/
/**********************************************************************************/
/**********************************************************************************/

/* File exit handler */
void do_file_exit(struct exec_context *ctx)
{
	/*TODO the process is exiting. Adjust the refcount
	of files*/
	int fd = 0;
	while (fd<MAX_OPEN_FILES){
		if (ctx->files[fd]!=NULL){
			if (fd<3){
				std_close(ctx->files[fd]);
				ctx->files[fd] = NULL;
			}
			else{
				do_file_close(ctx->files[fd]);
				ctx->files[fd] = NULL;
			}
		}
		fd++;
	}
}

/*Regular file handlers to be written as part of the assignmemnt*/


static int do_read_regular(struct file *filep, char * buff, u32 count)
{
	/** 
	*  TODO Implementation of File Read, 
	*  You should be reading the content from File using file system read function call and fill the buf
	*  Validate the permission, file existence, Max length etc
	*  Incase of Error return valid Error code 
	**/
	int ret_fd = -EINVAL; 

    if(!filep) //file existence
      return ret_fd; 

	struct inode* fil = filep->inode; //inode

    if ((!(fil->mode & O_READ)) || (!(filep->mode & O_READ))) // either does not have read permission
      return -EACCES;
    
    if ( fil->file_size > 4096 ) //max length check
      return -EOTHERS;

    int siz = fil->read(fil, buff, count, &(filep->offp));
    if (siz >= 0)
      filep->offp += siz;
    return siz;
}

/*write call corresponding to regular file */

static int do_write_regular(struct file *filep, char * buff, u32 count)
{
	/** 
	*   TODO Implementation of File write, 
	*   You should be writing the content from buff to File by using File system write function
	*   Validate the permission, file existence, Max length etc
	*   Incase of Error return valid Error code 
	* */
    int ret_fd = -EINVAL;
    if(!filep) //file existence
      return ret_fd;  

	struct inode* fil = filep->inode; //inode

    if ((!(fil->mode & O_WRITE )) || (!(filep->mode & O_WRITE))) // either does not have write permission
      return -EACCES;

	if ( fil->file_size > 4096 ) //max length check
		return -EOTHERS;

    int siz = fil->write(fil, buff, count, &(filep->offp));
    if (siz >= 0)
      filep->offp += siz;
	else if (siz<0)
		return -EOTHERS;
    return siz;
}

long do_file_close(struct file *filep)
{
	/** TODO Implementation of file close  
	*   Adjust the ref_count, free file object if needed
	*   Incase of Error return valid Error code 
	*/
	if(!filep)
		return -EINVAL;

	filep->ref_count--;
	if(!(filep->ref_count)) {        //last reference dropped
		filep->inode->ref_count--;       
		free_file_object(filep);
		filep = NULL;
	}
	return 0;
}

static long do_lseek_regular(struct file *filep, long offset, int whence)
{
	/** 
	*   TODO Implementation of lseek 
	*   Set, Adjust the ofset based on the whence
	*   Incase of Error return valid Error code 
	* */
	int ret_fd = -EINVAL; 
	if(!filep)
      return ret_fd;

	long set_p = 0;
    if(whence == SEEK_SET ){
      set_p = offset ;
    }
    else if (whence ==  SEEK_CUR )
    {
      set_p = offset + filep->offp;
    }
    else if (whence == SEEK_END){
      set_p = offset + (filep->inode->max_pos - filep->inode->s_pos );
    }
    if (set_p > (filep->inode->e_pos - filep->inode->s_pos) || (set_p < 0))
      return -EINVAL;
    else
    {
      filep->offp = set_p; //setting new offset pointer
      return set_p;
    }
	
}

extern int do_regular_file_open(struct exec_context *ctx, char* filename, u64 flags, u64 mode)
{

	/**  
	*  TODO Implementation of file open, 
	*  You should be creating file(use the alloc_file function to creat file), 
	*  To create or Get inode use File system function calls, 
	*  Handle mode and flags 
	*  Validate file existence, Max File count is 16, Max Size is 4KB, etc
	*  Incase of Error return valid Error code 
	* */

	int ret_fd = -EINVAL; 
    
    struct inode* fil;
    fil = lookup_inode(filename);
    if(!fil) //no existing file
	{
      if (flags & O_CREAT ){ //o_create flag is there
        fil = create_inode(filename, mode); //open the existing file
        if (!fil) //fil is NULL
          return -ENOMEM;
        flags = flags ^ O_CREAT; //exclude o_creat flag from flags

      }
      else
          return ret_fd;
	}

	//fil strores the file_fd at this point (either existing or newly created)

	if (flags & O_CREAT )
		flags = flags ^ O_CREAT; //removing O_CREAT if it exists

	if ( (!(fil->mode & O_READ)) &&(flags & O_READ) )
		return -EACCES;
	else if( (!(fil->mode & O_WRITE)) &&(flags & O_WRITE) )
		return -EACCES;
	else if ( (!(fil->mode & O_EXEC)) &&(flags & O_EXEC) )
		return -EACCES;
		

    if (fil->file_size > 4096)
      return -EOTHERS;
   
    struct file* newfile = alloc_file();
    newfile->type = REGULAR;
    newfile->mode = flags;
    newfile->inode = fil;
    newfile->offp = 0;

    int free_fd = 3; //starting from 3
    while((ctx->files[free_fd]) && (free_fd < MAX_OPEN_FILES))
    	free_fd++; //find first unassigned fd

    if(free_fd>=MAX_OPEN_FILES)
	{
    	return -EOTHERS;
	}
    else
    {
        ctx->files[free_fd] = newfile; //first unassigned fd
        newfile->ref_count = 1;
    }

    fil->ref_count++;
    newfile->fops->read = do_read_regular;
    newfile->fops->write = do_write_regular;
    newfile->fops->lseek = do_lseek_regular;
	newfile->fops->close = do_file_close;
  return free_fd;
}

/**
 * Implementation dup 2 system call;
 */
int fd_dup2(struct exec_context *current, int oldfd, int newfd)
{
	/** 
	*  TODO Implementation of the dup2 
	*  Incase of Error return valid Error code 
	**/
	int ret_fd = -EINVAL; 
	if ((oldfd>=MAX_OPEN_FILES) || (newfd>=MAX_OPEN_FILES) || (oldfd<0) || (newfd<0))
    	return ret_fd; //wrong value of old fd or new fd

	if(newfd == oldfd)
    	return newfd; //same fd's 

	if (!(current->files[oldfd])) //old fd is NULL
		return ret_fd;

	if(!(current->files[newfd])){ //newfd is NULL
		current->files[newfd] = current->files[oldfd];
		current->files[newfd]->ref_count++; //one more ref added
	}

	else{

		long ret;
		// if (newfd < 3){
		// 	ret = std_close(current->files[newfd]);
		// 	}
		// else{
		ret = do_file_close(current->files[newfd]);
		if (ret<0)
			return (int)ret;

		current->files[newfd] = current->files[oldfd];
		current->files[newfd]->ref_count++; //one more ref added

	}
	return newfd;
}

int do_sendfile(struct exec_context *ctx, int outfd, int infd, long *offset, int count) {
	/** 
	*  TODO Implementation of the sendfile 
	*  Incase of Error return valid Error code 
	**/

	if (!(ctx->files[infd]) || !(ctx->files[outfd])){
		return -EINVAL;
	}
	if (!(ctx->files[infd]->mode & O_READ))
		return -EACCES;
	if (!(ctx->files[outfd]->mode & O_WRITE))
		return -EACCES;


	char * buff = (char *)alloc_memory_buffer(); //to store the message
	int r_siz = 0, w_siz = 0;
	int tmp;
	if (offset == NULL){
		tmp = ctx->files[infd]->offp; //old file offset saved
		r_siz = do_read_regular(ctx->files[infd], buff, count);
		if (r_siz <0){
			return -EINVAL;
		}
		w_siz = do_write_regular(ctx->files[outfd], buff, r_siz);
		if (w_siz <0){
			return -EINVAL;
		}
		ctx->files[infd]->offp = tmp + w_siz; //offset reset to number of bytes written
	}

	else{
		tmp = ctx->files[infd]->offp; //old file offset saved
		ctx->files[infd]->offp = *offset; //starting offset
		r_siz = do_read_regular(ctx->files[infd], buff, count);
		if (r_siz <0){
			return -EINVAL;
		}
		w_siz = do_write_regular(ctx->files[outfd], buff, r_siz);
		if (w_siz <0){
			return -EINVAL;
		}
		*offset += w_siz; 
		ctx->files[infd]->offp = tmp; //offset reset to original
	}

	free_memory_buffer((void *)buff); //free the memory
	return w_siz;
}

