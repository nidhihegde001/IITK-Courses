#include <msg_queue.h>
#include <context.h>
#include <memory.h>
#include <file.h>
#include <lib.h>
#include <entry.h>



/************************************************************************************/
/***************************Do Not Modify below Functions****************************/
/************************************************************************************/

struct msg_queue_info *alloc_msg_queue_info()
{
	struct msg_queue_info *info;
	info = (struct msg_queue_info *)os_page_alloc(OS_DS_REG);
	
	if(!info){
		return NULL;
	}
	return info;
}

void free_msg_queue_info(struct msg_queue_info *q)
{
	os_page_free(OS_DS_REG, q);
}

struct message *alloc_buffer()
{
	struct message *buff;
	buff = (struct message *)os_page_alloc(OS_DS_REG);
	if(!buff)
		return NULL;
	return buff;	
}

void free_msg_queue_buffer(struct message *b)
{
	os_page_free(OS_DS_REG, b);
}

/**********************************************************************************/
/**********************************************************************************/
/**********************************************************************************/
/**********************************************************************************/


int do_create_msg_queue(struct exec_context *ctx)
{
	/** 
	 * TODO Implement functionality to
	 * create a message queue
	 **/
	int fd = 0;
	for (fd = 0; fd< MAX_OPEN_FILES; fd++){
		if (ctx->files[fd]==NULL){
			break;
		} // fd stores the file descriptor of message queue
	}
	struct file * fil = alloc_file();
	fil->fops = NULL;
	fil->ref_count = 1;
	struct msg_queue_info * mqi = alloc_msg_queue_info();
	if (!mqi)
		return -ENOMEM;
	mqi->num_msg = 0; // 0 messages yet
	mqi->member_count = 1; //only calling process a member yet
	mqi->msg_buffer = alloc_buffer(); //stores all messages in this space
	struct message * tmp = mqi->msg_buffer;
	for (int i = 0; i<512; i++){
		tmp->from_pid = -1; //initialize
		tmp->to_pid = -1;
		tmp->msg_txt[0] = '\0';
		tmp++;
	}

	mqi->member_pid[0] = ctx->pid; 
	for (int i = 1; i < MAX_MEMBERS; i++){
		mqi->member_pid[i] = 10; //initialization of member_pid array
	}
	for (int i = 0; i < 8; i++){
		for (int j = 0; j<8; j++){
			mqi->blocked[i][j] = 0; //no process is blocked initially 
		}
	}
	if (!(mqi->msg_buffer))
		return -ENOMEM;

	fil->msg_queue  = mqi;
	ctx->files[fd] = fil;
	return fd;
}

//Helper function
void delete_message_from_queue(struct message * msg_q){
	//delete the struct message pointed to by it;
	//by re-initializing that message
	msg_q->from_pid = -1;
	msg_q->to_pid = -1;
	msg_q->msg_txt[0] = '\0';
	struct message * tmp = msg_q + 1;
	while (tmp->from_pid != -1){
		msg_q->from_pid = tmp->from_pid;
		msg_q->to_pid = tmp->to_pid;
		for (int c = 0; c<MAX_TXT_SIZE; c++)
			msg_q->msg_txt[c] = tmp->msg_txt[c];
		tmp++; //left shift operation
		msg_q++;
	}	
	msg_q->from_pid = -1;
	msg_q->to_pid = -1;
	msg_q->msg_txt[0] = '\0'; //delete last element which is already shifted left
}

int do_msg_queue_rcv(struct exec_context *ctx, struct file *filep, struct message *msg)
{
	/** 
	 * TODO Implement functionality to
	 * recieve a message
	 **/
	int to_process_pid = ctx->pid;

	if (filep->msg_queue == NULL)
		return -EINVAL;

	struct message *msg_q  = filep->msg_queue->msg_buffer;  //starting of message allocation
	if (msg_q->from_pid == -1)
		return 0; //no messages in the queue

	while(msg_q->from_pid != -1){
		// if (msg_q->to_pid == BROADCAST_PID){
		// 	msg->from_pid = msg_q->from_pid;
		// 	msg->to_pid = msg_q->to_pid;
		// 	for (int c = 0; c<MAX_TXT_SIZE; c++)
		// 		msg->msg_txt[c] = msg_q->msg_txt[c];  // receive and fill the msg
		// 	return 1;
		// }

		if (msg_q->to_pid == to_process_pid){
			msg->from_pid = msg_q->from_pid;
			msg->to_pid = msg_q->to_pid;
			for (int c = 0; c<MAX_TXT_SIZE; c++)
				msg->msg_txt[c] = msg_q->msg_txt[c];   // receive and fill the msg
			//delete the message from the queue
			//shift the array of remaining messages to the left
			delete_message_from_queue(msg_q);
			return 1; // just receive the first message addressed and return
		}
		msg_q++;
	}

	return 0;
}


int do_msg_queue_send(struct exec_context *ctx, struct file *filep, struct message *msg)
{
	/** 
	 * TODO Implement functionality to
	 * send a message
	 **/
	int from_process_pid = ctx->pid;
	// if (from_process_pid != msg->from_pid)
	// 	return -EINVAL; // is it assumed?????
	if (filep->msg_queue == NULL)
		return -EINVAL;
	
	struct message *msg_q  = filep->msg_queue->msg_buffer;  //starting of message allocation
	while(msg_q->from_pid !=-1){
		msg_q++; //reach the end of queue
	}

	int from_found=0, to_found = 0;
	for (int from = 0; from < filep->msg_queue->member_count; from++){
		if (filep->msg_queue->member_pid[from] == msg->from_pid){
			from_found = 1; //from_pid process in the message queue
			break;
		}
	}
	if (!from_found)
		return -EINVAL; //from_pid not found in members

	int num_sent = 0;
	
	if (msg->to_pid != BROADCAST_PID){
		for (int to = 0; to < filep->msg_queue->member_count; to++){
			if (filep->msg_queue->member_pid[to] == msg->to_pid){
				to_found = 1; //to_pid process in the message queue
				break;
			}
		}
		if (!to_found)
			return -EINVAL; //to_pid not found in members

	   //blocked process
		if (filep->msg_queue->blocked[msg->from_pid][msg->to_pid] == 1){
			return -EINVAL;
		}

		msg_q->from_pid = msg->from_pid;
		msg_q->to_pid = msg->to_pid;
		for(int c = 0; c<MAX_TXT_SIZE; c++)
			msg_q->msg_txt[c] = msg->msg_txt[c]; //register a message at the end of queue
		return 1;
	}

	else if(msg->to_pid == BROADCAST_PID){

		// if (filep->msg_queue->member_count == 1) //no member in queue
		// 	return 0;

		for (int to = 0; to < filep->msg_queue->member_count; to++){
			// do for all processes except calling process and blocked processes
			if ((filep->msg_queue->member_pid[to]!= msg->from_pid) && (!(filep->msg_queue->blocked[msg->from_pid][filep->msg_queue->member_pid[to]]))){
				msg_q->to_pid = filep->msg_queue->member_pid[to];
				msg_q->from_pid = msg->from_pid;
				for (int c = 0; c<MAX_TXT_SIZE; c++)
					msg_q->msg_txt[c] = msg->msg_txt[c];
				msg_q++; //go to next unassigned message and assign it
				num_sent++;
			}
		}
		return num_sent;	
	}

	//return -EINVAL;
}

void do_add_child_to_msg_queue(struct exec_context *child_ctx)
{
	/** 
	 * TODO Implementation of fork handler 
	 **/
	int i = 0;
	for (i = 0; i< MAX_OPEN_FILES; i++){
		if (child_ctx->files[i] !=NULL){ 
			struct msg_queue_info * msg_q = (child_ctx->files[i])->msg_queue;
			if (msg_q!=NULL){
				msg_q->member_pid[msg_q->member_count] = child_ctx->pid; //add child pid
				msg_q->member_count +=1; //increase member count
			}
			child_ctx->files[i]->ref_count +=1; //fd's of child increment ref by 1
		}
	}
}

void do_msg_queue_cleanup(struct exec_context *ctx)
{
	/** 
	 * TODO Implementation of exit handler 
	 **/
	int i = 0;
	for (i = 0; i< MAX_OPEN_FILES; i++){
		if (ctx->files[i] !=NULL){ 
			struct msg_queue_info * mqi = ctx->files[i]->msg_queue;
			if (mqi != NULL){ // a message queue exists at this fd
				do_msg_queue_close(ctx, i );
			}
		}
	}

}

int do_msg_queue_get_member_info(struct exec_context *ctx, struct file *filep, struct msg_queue_member_info *info)
{
	/** 
	 * TODO Implementation of exit handler 
	 **/
	if (filep->msg_queue == NULL)
		return -EINVAL;
// use of ctx???
	info->member_count = filep->msg_queue->member_count;
	int num = 0;
	for (int m = 0; m<MAX_MEMBERS; m++){
		if (filep->msg_queue->member_pid[m] == 10) // no process pointed to
			continue;
		info->member_pid[num++] = filep->msg_queue->member_pid[m];
	}
	return 0;
}


int do_get_msg_count(struct exec_context *ctx, struct file *filep)
{
	/** 
	 * TODO Implement functionality to
	 * return pending message count to calling process
	 **/
	if (filep->msg_queue == NULL)
		return -EINVAL;


	
	int found = 0; // to verify if given process is a member of message queue
	for (int i = 0; i < MAX_MEMBERS; i++){
		if (filep->msg_queue->member_pid[i] == ctx->pid){
			found = 1;
			break; //i stores the index of the process pid
		}
	}
	if (!found)
		return -EINVAL; 
		
	int num = 0;
	struct message * msg_q = filep->msg_queue->msg_buffer;
	while(msg_q->from_pid != -1){
		//iterating thru all messages
		if (msg_q->to_pid == ctx->pid){
			num++; //message addressed to this process
		}
		msg_q++; //go to next message
	}

	return num;
}

int do_msg_queue_block(struct exec_context *ctx, struct file *filep, int pid)
{
	/** 
	 * TODO Implement functionality to
	 * block messages from another process 
	 **/

	int process_pid = ctx->pid;

	if (filep->msg_queue == NULL)
		return -EINVAL;

	int to_found = 0;
	for (int to = 0; to < filep->msg_queue->member_count; to++){
		if (filep->msg_queue->member_pid[to] == pid){
			to_found = 1;
			break;
		}
	}
	if (!to_found)
		return -EINVAL; //pid not found in members

	filep->msg_queue->blocked[pid][process_pid] = 1; //block the process pid
	return 0;
}

int do_msg_queue_close(struct exec_context *ctx, int fd)
{
	/** 
	 * TODO Implement functionality to
	 * remove the calling process from the message queue 
	 **/
	int process_pid = ctx->pid;
	struct file * fil = ctx->files[fd];
	if (fil==NULL)
		return -EINVAL;
	struct msg_queue_info * mqi = fil->msg_queue;
	if (mqi==NULL)
		return -EINVAL;

	int i=0, found = 0; // to verify if given process is a member of message queue
	for (i = 0; i < MAX_MEMBERS; i++){
		if (mqi->member_pid[i] == process_pid){
			found = 1;
			break; //i stores the index of the process pid
		}
	}
	if (!found)
		return -EINVAL; // IS it needed???? Calling process not a queue member
		
	if (found){
		mqi->member_count -= 1;
		for (int j = i+1; j<MAX_MEMBERS; j++){
			mqi->member_pid[j-1] = mqi->member_pid[j]; //left shift
		}
		mqi->member_pid[MAX_MEMBERS-1] = 10; // re-initialized to point to no process
	}
	// free from message queue
	struct message* msg_q = mqi->msg_buffer;
	while(msg_q->from_pid != -1){
		//iterating thru all messages
		if (msg_q->to_pid == process_pid){
			//message addressed to this process, then delete it from the queue
			delete_message_from_queue(msg_q);
		}
		msg_q++; //go to next message
	}
	
	if (mqi->member_count == 0){
		free_msg_queue_buffer(mqi->msg_buffer);
		free_msg_queue_info(mqi);
		free_file_object(ctx->files[fd]);
		// mqi = NULL;
	}

	ctx->files[fd] = NULL;
	
	return 0;
}
