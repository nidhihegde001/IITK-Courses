
#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>


int main(int argc, char* argv[]) 
 {
   int pid, n_char;
   int fd_i[2];
   int fd_o[2];
   char buf1[32];
   char buf2[32];
    
    
     // PLAYER 1
   if(pipe(fd_i) < 0){
        perror("pipe");
        exit(-1);
   }
    
  if(pipe(fd_o) < 0){
        perror("pipe");
        exit(-1);
   }
       
   pid = fork();   
   if(pid < 0){
      perror("fork");
      exit(-1);
   }  

   if(!pid){ // Child 
       close(fd_o[0]);      // Close the read end in child for writing output to pipe
       close(fd_i[1]);      // Close the write end in child for taking input GO
       dup2(fd_i[0], 0); 
	   dup2(fd_o[1], 1); 

       if (execl(argv[1],"player1", NULL)){
           perror("execl");
           exit(-1);
       }
   }
   
   if (pid){
       close(fd_o[1]);    // Close the write end in the parent for taking output from child
       close(fd_i[0]);    // Close the read end in the parent for giving input
        for (int i = 0; i<10; i++){
           if (write(fd_i[1], "GO", 3)!=3){
                perror("write");
                exit(-1); 
           }
           read(fd_o[0], buf1+i, 1);
        //    printf("%c",*(buf1+i));
        }
        // n_char = read(fd_o[0], buf1, 11);
        n_char = 10;
        // printf("%d",n_char);
        close(fd_o[0]);
        close(fd_i[1]);
   }
    
    
	if(n_char!=10){
		perror("read");
		exit(-1);
   }
   
  buf1[n_char] = '\0';
//   printf("%s\n", buf1);
    
    // PLAYER 2
    if(pipe(fd_i) < 0){
        perror("pipe");
        exit(-1);
   }
    
  if(pipe(fd_o) < 0){
        perror("pipe");
        exit(-1);
   }
       
   pid = fork();   
   if(pid < 0){
      perror("fork");
      exit(-1);
   }  

   
   if(!pid){ // Child 
       close(fd_o[0]);      // Close the read end in child for writing output to pipe
       close(fd_i[1]);      // Close the write end in child for taking input GO
       dup2(fd_i[0], 0); 
	   dup2(fd_o[1], 1); 

       if (execl(argv[2],"player2", NULL)){
           perror("execl");
           exit(-1);
       }
   }
   
   close(fd_o[1]);    // Close the write end in the parent for taking output from child
   close(fd_i[0]);    // Close the read end in the parent for giving input
    for (int i = 0; i<10; i++){
           if (write(fd_i[1], "GO", 3)!=3){
                perror("write");
                exit(-1); 
           }
           read(fd_o[0], buf2+i, 1);
        //    printf("%c",*(buf2+i));
        }
        // n_char = read(fd_o[0], buf1, 11);
        n_char = 10;
        // printf("%d",n_char);
	//  n_char = read(fd_o[0], buf2, 11);
     close(fd_o[0]);                                //close remaining ends of both pipes
     close(fd_i[1]);
            
	if(n_char!=10){
		perror("read");
		return -1;
   }
   
  buf2[n_char] = '\0';
//   printf("%s\n", buf2);
    
    int s1 = 0, s2 = 0;
    for (int i = 0; i<10; i++){
        if (buf1[i]>buf2[i]){
            if (buf1[i]-buf2[i] == 1)
                s1++;
            else
                s2++;
        }
         else if (buf2[i]>buf1[i]){
            if (buf2[i]-buf1[i] == 1)
                s2++;
            else
                s1++;
        }
        
        else {
            continue;
        }
    }
    
    printf("%d %d", s1, s2);
    return 0;
}