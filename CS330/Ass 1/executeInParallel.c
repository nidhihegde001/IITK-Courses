#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include<unistd.h>
#include <string.h>
#include <wait.h>

int execute_in_parallel(char *infile, char *outfile)
{
   int pid, n_char, i_fd, o_fd;
   char in_buf[5000];
   char buf[5000];
   int fd[2];
   char *cmd = malloc(sizeof(char)*32);
   i_fd = open(infile , O_RDONLY);   // input file
//    dup2(i_fd, 0);                  //connecting stdin to input file
   o_fd = open(outfile , O_RDWR|O_CREAT, 0666);         //output file
//    printf("output file %d", o_fd);

    
    if (read(i_fd,in_buf, 5000)<0){
        printf("unable to read input file\n");
         perror("read");
         return -1;
    }
    cmd = strtok(in_buf, "\n");
    
    close(i_fd);
    
   while (cmd !=NULL){ 
    if(pipe(fd) < 0){
        perror("pipe");
        return -1;
        } 
       
    pid = fork();
    if (pid)
        close(fd[1]); // Close the write end in the parent
       
    if(pid < 0){
     perror("fork");
     return -1;
        }
       
    if(!pid){ /*Child*/
        close(fd[0]); // Close the read end in the child
        dup2(fd[1],1); //connect stdout of child to write end of pipe
        char *env = malloc(512*sizeof(char));
        char * tok;
        char *tmp = malloc(128*sizeof(char));
        char * arg[7];
        tok = strtok (cmd," ");
        int i = 0;
        while (tok != NULL)
        {
            arg[i] = tok;
            i++;
            tok = strtok (NULL, " ");
        }
        char ** argv = malloc(sizeof(char*)*(i+1));
         for (int j = 0; j<i; j++)
            argv[j] = arg[j];
        
        argv[i] = NULL;
        env = getenv("CS330_PATH");
        if (env ==NULL){
            printf ("UNABLE TO EXECUTE\n");
            exit(-1);
        }
        tok = strtok (env,":");
        while (tok != NULL)
        {
            strcpy (tmp,tok);
            strcat(tmp,"/");
            strcat(tmp, argv[0]);
            if(execv(tmp, argv))
                tok = strtok (NULL, ":");
        }
        printf("UNABLE TO EXECUTE\n");
        exit(-1);
    }
    
    if (pid){
         if((n_char = read(fd[0], buf, 5000))<0)   // buf contains the value to be written to o_fd
            return -1;
       
        close(fd[0]); // Close the read end in the parent
        buf[n_char] = '\0';
//         printf("hi\n");
        if(write(o_fd, buf, n_char)<0){
                perror("write");
                exit(-1);
            }

        cmd = strtok(NULL, "\n");
        }
   }
    close(o_fd);
    return 0;
}
       
int main(int argc, char *argv[])
{
    return execute_in_parallel(argv[1], argv[2]);
}
