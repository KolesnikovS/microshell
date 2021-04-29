#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <dirent.h>

//tokens - words or special charecters
typedef enum {T_WORD, T_PIPE, T_GT, T_LT, T_NL, T_EOF} token; 

//search with meta-characters
int match_pattern(char *word, char *pattern);

//iterate/ recursive function
char** iterate(char *path, char *pattern, char **argv, int *argc, int *size);

//execute functions
pid_t execute(int argc, char **argv, int infd, char *in_file, int outfd, char *out_file);

//searches for all the pathnames matching pattern, writes the results to argv
//like in glob function
char** glob(char *pattern, char **argv, int *argc, int *size);


//read line and split it into tokens
token read_split(char *word);

//operate
token command(int *waitpid, int makepipe,int *pipefdp);




//read the line and split it into tokens
token read_split(char *word) {
    char c, status = 0;
    while((c = getchar()) != EOF) {          //read to the end
    	if(status){                             //in a word
            switch(c) {
                case '|':
                case '<':
                case '>':
                case '\n':
                case ' ':
                case '\t':
                    *word = 0;
                    ungetc(c, stdin);
                    return T_WORD;
                default:
                    *word++ = c;
                    continue;
            }
        }
        else{
            switch(c) {
                case '|':
                	return T_PIPE;
                case '<':
                    return T_LT;
                case '>':
                    return T_GT;
                case '\n':
                    return T_NL;
                case ' ':
                case '\t':
                	continue;
                default:
                    status = 1;
                    *word++ = c;
                    continue;
                }

    	}
    }
    return T_EOF;
}



//search with meta-characters
int match_pattern(char *word, char *pattern) {  
                           
    if(*word == 0) return *pattern == 0 | *pattern == '/'; 
    switch(*pattern) {
        case '*':
        {
            char *w = word;
            do{
                if(match_pattern(w, pattern+1))
                    return 1;
            }	while(*w++);
            return 0;
        }
        case '?':
            return match_pattern(word+1, pattern+1);
        default:
            return *word == *pattern && match_pattern(word+1, pattern+1);
    }
}


char** iterate(char *path, char *pattern, char **argv, int *argc, int *size) {
    DIR *dir = opendir(path);
    if(dir == 0)
        return argv;

    struct dirent *de;
    char *patt = pattern;       //recursive iteration pattern

    while(*patt != 0 & *patt != '/')
        patt++;

    int rec_point = *patt != 0;
    patt++;

    while ((de = readdir(dir)) != 0) {
        if(!(strcmp(de->d_name, ".") & strcmp(de->d_name, "..")))
            continue;

        if(!match_pattern(de->d_name, pattern))
            continue;

        int plen = strlen(path), nlen = strlen(de->d_name);
        if(!rec_point) {
            if(*argc >= *size-1) {
                *size *= 2;
                argv = realloc(argv, *size * sizeof(char*));
            }
            argv[*argc] = malloc(plen + nlen + 1);
            strcpy(argv[*argc], path);
            strcpy(argv[*argc]+plen, de->d_name);
           (*argc)++;
        }

        if((de->d_type == DT_DIR) & rec_point) {
            char* re_path = malloc(plen + nlen + 2);
            strcpy(re_path, path);
            strcpy(re_path + plen, de->d_name);
            re_path[plen + nlen] = '/';
            re_path[plen + nlen + 1] = 0;
            argv = iterate(re_path, patt, argv, argc, size);
            free(re_path);
        }
    }
    free(dir);
    return argv;
}







//searches for all the pathnames matching pattern, writes the results to argv
//like in glob function
char** glob(char *pattern, char **argv, int *argc, int *size) {
    char *path;
    char *patt = pattern;

    if(*pattern == '/') {
        pattern++;
        path = malloc(2);
        path[0] = '/';
        path[1] = 0;
    } else {
        if( (pattern[0] == '.') & (pattern[1] == '/') )
            pattern += 2;
        path = malloc(3);
        strcpy(path, "./");
    }

    int argc_ = *argc;
    argv = iterate(path, pattern, argv, argc, size);

    //returning original pattern if there was no match
    if(argc_ == *argc) {
        if(*argc >= *size-1) {
            *size *= 2;
            argv = realloc(argv, *size * sizeof(char*));
        }
        strcpy(argv[(*argc)++] = malloc(strlen(patt)+1), patt);
    }
    free(path);
    return argv;
}


//check if it's an internal function and execute
void (*sig)(int);
pid_t execute(int argc, char **argv, int infd, char *in_file, int outfd, char *out_file) {
    pid_t pid;

    //internal functions
    if(argc == 0) return 0;

    if(!strcmp(argv[0], "pwd")) {
        char buff[1000];
        printf("%s\n", getcwd(buff, 1000));
        return 0;
    }
    if(!strcmp(argv[0], "cd")) {
        if(chdir(argv[1]))
            printf("Can't change to directory \'%s\'\n", argv[1]);
        return 0;
    }


    if(!strcmp(argv[0], "time")) {
        struct timespec t1, t2;
        clock_gettime(CLOCK_MONOTONIC, &t1);
        pid_t pid = 0;
        int status;
        if((pid = execute(argc - 1, argv+1, infd, in_file, outfd, out_file)))
            while(wait(&status) != pid);
        clock_gettime(CLOCK_MONOTONIC, &t2);
        float elapsed = t2.tv_sec - t1.tv_sec;
        elapsed += (t2.tv_nsec - t1.tv_nsec) / 1e9;
        printf("\ntime: %.3fs\n", elapsed);
        return 0;
    }

    
    // execute other via execvp
    switch(pid = fork()) {
        case -1:
            printf("Fork error");

        case 0:
            signal(SIGINT, sig);        // ctrl+c
            
            //chechking stdin & stdout
            if(infd != 0) {
                close(0);
                if(infd > 0)
                    dup(infd);
                else if(open(in_file, O_RDONLY) == -1){
                    printf("Can't open %s\n", in_file);
                    exit(0);
                }
            }

            if(outfd != 1) {
                close(1);
                if(outfd > 0)
                    dup(outfd);
                else if(open(out_file, O_WRONLY | O_CREAT, 00777) == -1){ 
                    printf("Can't open %s\n", out_file);
                    exit(0);
                }
            }
               
            //execute   
            execvp(argv[0], argv);
            
            printf("Can't execute %s\n", argv[0]);
            exit(0);

        default:
            return pid;
    }
}



token command(int *waitpid, int makepipe,int *pipefdp){
    int argvsize = 1024, fail = 0, infd = 0, outfd = 1, argc = 0, pfd[2]; 
    char word[1024], in_file[1024], out_file[1024];      
    token t;
    char **argv = calloc(argvsize, sizeof(char*));   

    while(1) {
        switch(t = read_split(word)) {
            case T_WORD:

                argv = glob(word, argv, &argc, &argvsize);
                continue;

            case T_LT:

                if(infd != 0) { 
                    printf("Extra <\n");
                    break;
                }


                if(read_split(in_file) != T_WORD) {
                    printf("%s is not a filename!", in_file);
                    fail = 1;
                    break;
                }

                infd = -2;
                continue;

            case T_GT:  

                if(outfd != 1) {
                    printf("Extra <!\n");
                    fail = 1;
                    break;
                }

                if(read_split(out_file) != T_WORD) {
                    printf("%s is not a filename!", out_file);          
                    fail = 1;
                    break;
                }

                outfd = -2;
                continue;

            case T_PIPE:
            case T_NL:
                argv[argc] = 0;
                if(argc == 0)
                    break;

                if(outfd != 1 && t == T_PIPE) {
                    printf("Pipe after >\n");
                    fail = 1;
                    break;
                }

                if(t == T_PIPE && command(waitpid, 1, &outfd))
                    break;

                if(makepipe) {
                    if(pipe(pfd))
                    printf("Pipe error");
                    *pipefdp = pfd[1];
                    infd = pfd[0];
                }  

                pid_t pid = execute(argc, argv, infd, in_file, outfd, out_file);
                if(*waitpid == 0 && pid != 0)
                    *waitpid = pid;
                break;
            case T_EOF:
                printf("\n");
                break;
        }
        while(--argc >= 0)
            free(argv[argc]);
        free(argv);

        if(fail)                                      //Read the line to the end when the input is invalid. 
            while(t != T_NL & t != T_EOF)
                t = read_split(word);

        if(t == T_EOF)
            exit(0);

        return fail;
    }     

};


//looping
void microshell_loop(){

  	int status;
  	char buff[1024];
  	pid_t pid = 0;

	while(1){

  		if(geteuid() == 0)                             //root checking for the symbol
            printf("%s!", getcwd(buff, 1000));         //showing the directory
        else
            printf("%s>", getcwd(buff, 1000));
        if(!command(&pid, 0, 0) && pid) 
            while(wait(&status) != pid);
        pid = 0;


	}

}


int main(){  

  signal(SIGINT, SIG_IGN);             //ignore ctrl+c
  microshell_loop();
  return EXIT_SUCCESS;

}