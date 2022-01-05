#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include <string.h>
#include <wait.h>
int executeCommand (char *cmd) {
    pid_t pid;
    int status;
    pid = fork();
    if(pid < 0){
         perror("fork");
         exit(-1);
    }
        
    if(!pid){ /*Child*/
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
         for (int j = 0; j<i; j++){
            argv[j] = arg[j];
        }
        argv[i] = NULL;
        env = getenv("CS330_PATH");
        if (env ==NULL){
            printf ("UNABLE TO EXECUTE\n");
            return -1;
        }
    
        tok = strtok (env,":");
        while (tok != NULL)
        {
            strcpy (tmp,tok);
            strcat(tmp,"/");
            strcat(tmp, arg[0]);
            if(execv(tmp, argv)){
                tok = strtok (NULL, ":");
            }
        }
        printf("UNABLE TO EXECUTE\n");
        exit(-1);
    }
    wait(&status);
    return status;
}

int main (int argc, char *argv[]) {
    return executeCommand(argv[1]);
}