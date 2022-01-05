
#define ROCK        0 
#define PAPER       1 
#define SCISSORS    2 

#define STDIN 		0
#define STDOUT 		1
#define STDERR		2


#include "gameUtils.h"
#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <wait.h>

int getWalkOver(int numPlayers); // Returns a number between [1, numPlayers]

int main(int argc, char *argv[])
{
//     printf("%d\n", argc);
    int N = 10; // default number of rounds in each match
    int P;
    int nm = 1;
    int status;
    char content[100110]; // max file size is 100chars per line X 1000 lines + 100 buffer
    
//    
    if (argc == 4){
        N = atoi(argv[2]);
        nm = 3;
    }
    
    int p_fd, len=0;
    char *pl = malloc(sizeof(char)*100);          // temp player executable path 
    
    p_fd = open(argv[nm] , O_RDONLY);             // open players.txt
//     printf("p_fd: %d", p_fd);
    dup2(p_fd, 0);                                // connect it to stdin
    
    if (read(p_fd,content, 100110)<0){
         perror("read");
         return -1;
    }
    
    pl = strtok(content, "\n");
    P = atoi(pl);                             //number of players convert into integer
    // printf("%d", P);
    if (P<2)  //there can't be less than 2 players
        return -1;
    
    char **plist = malloc(sizeof(char*)*P);  //to store paths of all the players
    
    for (int i = 0; i<P; i++){               //next P lines contain path of executable
        plist[i] = malloc(sizeof(char)*100);
    }
        
    len = 0;
    int i = 0;
     while ((pl = strtok(NULL, "\n"))!=NULL){   //read line
//             for (int x = 0; pl[x]!='\0'; x++)
//                  len++;
//             if (len == 0)
//                 exit(-1);                     //command empty, so exit  
         plist[i++] = pl;           //storing the ith executable's path in the array plist at pos i
    }
    
    int g_pl_list[P];                       // global player list marking active players as 1
    int  w,j,odd, ap = P;
    
    for (int i = 0; i<P; i++){
        g_pl_list[i] = 1;                   //all are active initially
        printf("p%d", i);
        if (i!=P-1)
            printf(" ");
    }
    printf("\n");
    
    int level = P-1;                       // number of levels in tournament
    int curr_ap = ap;                      //no of active players currently
    
    while(level--){
        int *pl_list = malloc(sizeof(int)*ap);      //local array to store active players after Walkover
        if (curr_ap%2==1){
            odd = 1;
            w = getWalkOver(curr_ap);
            ap = 0;
            j = 0;
            for (int i = 0; i<P; i++){
                if ((g_pl_list[i]==1) && (ap!=w-1)) {      //if active and not the wth active player
                    pl_list[j++] = i;
                    ap++;
                 }
                else if (ap==w-1)
                    ap++;
            }
            
            // j stores no of elements in pl_list, final no of players playing in this round after walkover
            // ap will be j+1 at this point
        }  
        
        else{
            odd = 0;
            j = 0;
            for (int i = 0; i<P; i++){
                if (g_pl_list[i]==1)     //if active and not the wth active player
                    pl_list[j++] = i;
            }
        }
        
        //pl_list is created and must contain even number of elements at this point
        
        int p1, p2;           // 2 players
        int pid, n_char;
        int fd_i[2];
        int fd_o[2];
        char buf1[32];       // 32 is upper limit of number of matches in each round
        char buf2[32];
        
        for (int cp = 0; cp<ap-1; cp=cp+2){
            p1 = pl_list[cp];
            p2 = pl_list[cp+1];
            
           // PLAYER 1
            
            
           // get the name of executable from its path
            
           int k, pos=0;
           for (k = 0; plist[p1][k]!='\0'; k++){
               if (k == '/')
                   pos = k;
                   
           }
           if(pipe(fd_i) < 0){
                perror("pipe");
                return -1;
           }

          if(pipe(fd_o) < 0){
                perror("pipe");
                return -1;
           }

           pid = fork();   
            
           if(pid < 0){
              perror("fork");
              return -1;
           }  

           if(!pid){ // Child 
               close(fd_o[0]);                                  // Close the read end in child for writing output to pipe
               close(fd_i[1]);                                  // Close the write end in child for taking input GO
               dup2(fd_i[0], 0); 
               dup2(fd_o[1], 1); 

               if (execl(plist[p1], plist[p1] + pos + 1, NULL)){
                   perror("execl");
                   exit(-1);
               }
           }

           if (pid){
               close(fd_o[1]);                                // Close the write end in the parent for taking output from child
               close(fd_i[0]);                                // Close the read end in the parent for giving input
                for (int i = 0; i<N; i++){
                    if (write(fd_i[1], "GO", 3)!=3){
                            perror("write");
                            exit(-1); 
                    }
                    read(fd_o[0], buf1+i, 1);
                    //    printf("%c",*(buf1+i));
                }
                n_char = N;
                close(fd_o[0]);                                //close remaining ends of both pipes
                close(fd_i[1]);
           }

            if(n_char<0){
                perror("read");
                return -1;
           }
          buf1[n_char] = '\0';
        //   printf("%s\n", buf1);

            
            
            
            // PLAYER 2
            pos = 0;
           for (k = 0; plist[p2][k]!='\0'; k++){
               if (k == '/')
                   pos = k;
                   
           }
            if(pipe(fd_i) < 0){
                perror("pipe");
                return -1;
           }

            if(pipe(fd_o) < 0){
                perror("pipe");
                return -1;
           }

           pid = fork(); 

           if(pid < 0){
              perror("fork");
              return -1;
           }  


           if(!pid){ // Child 
               close(fd_o[0]);                                  // Close the read end in child for writing output to pipe
               close(fd_i[1]);                                  // Close the write end in child for taking input GO
               dup2(fd_i[0], 0); 
               dup2(fd_o[1], 1); 

               if (execl(plist[p2], plist[p2] + pos+1 , NULL)){
                   perror("execl");
                   exit(-1);
               }
           }

           close(fd_o[1]);                                // Close the write end in the parent for taking output from child
           close(fd_i[0]);                                // Close the read end in the parent for giving input
                for (int i = 0; i<N; i++){
                    if (write(fd_i[1], "GO", 3)!=3){
                            perror("write");
                            exit(-1); 
                    }
                    read(fd_o[0], buf2+i, 1);
                    //    printf("%c",*(buf1+i));
                }
                n_char = N;
            close(fd_o[0]);                                //close remaining ends of both pipes
            close(fd_i[1]);
            
            if(n_char<N){
                perror("read");
                exit(-1);
           }
          buf2[n_char] = '\0';
        //   printf("%s\n", buf2);

            int s1 = 0, s2 = 0;
            for (int i = 0; i<N; i++){
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
            }

            if (s1<s2)
                g_pl_list[p1] = 0;
            else
                g_pl_list[p2] = 0;
        }
        
    //Wait for all matches to finish in the current level
        
        wait(NULL);
     // Calculate number of active players now
    curr_ap = 0;
    for (int i = 0; i<P; i++){
        if (g_pl_list[i]==1){
            curr_ap++;
        }
    }
        // Print the active players
    ap = 0;
    for (int i = 0; i<P; i++)
         if (g_pl_list[i]==1){
            printf("p%d", i);
            ap++;
            if (ap !=curr_ap)
                printf(" ");
        }
         
    if (level!=0)
        printf("\n");
    free(pl_list);      // free the local array of active players
  }
    return 0;
}
