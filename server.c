#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <ftw.h>
#include <dirent.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <tar.h>
#define PORT 4000

int skt;  // for socket creation
char source[50] = "/home/";  // root path
char *firstarg = '\0';  // first argument of received command
int size1;  // for getfz
int size2;  // for getfz
char r_value_fn[1000];  // it will contain return value for getfn command
char filepaths[1000000];  // it will contain searched file paths
char *args[4] = {NULL};  // arguments array of command
int argc;  // arguments count

// signal handler
void signalHandler(int sig){
    close(skt);  // close socket
    exit(sig);  // exit process
}

// get arguments from command using tokenization by space and store it in args array, return argc
int getArgs(char *cmd, char *args[]){
    char *token = strtok(cmd, " ");
    int argc = 0;
    
    while (token != NULL){
        args[argc] = strdup(token);
        token = strtok(NULL, " ");
        argc++;
    }
    
    return argc;
}

// check if found file path is exists or not
int fileExists(const char *filename) {
    struct stat st;
    return stat(filename, &st) == 0;  // stat function to check if file exists or not
}

// this function will get the size of given file path using ls command
int getFileSize(const char *filename){
    struct stat st;

    if (stat(filename, &st) == 0){  // checking if file exists or not
        char cmd[100];
        snprintf(cmd, sizeof(cmd), "ls -l \"%s\" | awk '{printf $5}'", filename);  // store command in cmd
        FILE *f = popen(cmd, "r");  // open and run command
        char op[8];
        fgets(op, sizeof(op), f);  // store output in op
        pclose(f);
        
        return atoi(op);  // convert op to int and return
    }
    else{
        return -1;  // if file doesn't exists then return -1
    }
}

// function to get file permissions using ls command mostly same as getFileSize
char *getPermission(const char *filename){
    char cmd[100];
    snprintf(cmd, sizeof(cmd), "ls -l \"%s\" | awk '{print $1}'", filename);  // store command
    FILE *f = popen(cmd, "r");  // run
    char *op = (char *)malloc(50 * sizeof(char));
    fgets(op, 50, f);  // store in op
    pclose(f);

    // Remove the newline character at the end, if present
    char *newline_pos = strchr(op, '\n');
    if (newline_pos != NULL) {
        *newline_pos = '\0';
    }

    return op;  // return found permissions
}

// function to create tar file
void createTar(int clt){
    
    char cmd[200];
    char tar_name[20];
    sprintf(tar_name, "%d", clt);
    strcat(tar_name, "stemp.tar.gz");  // name of tar file file_discriptor+s+temp.tar.gz, it is a temp file. It will create different files for all the client requests

    snprintf(cmd, sizeof(cmd), "tar -czf \"%s\" --transform='s#.*/##' -P -T -", tar_name);  // create tar using tar command
    FILE *f = popen(cmd, "w");  // run
    if (f == NULL) {
        fprintf(stderr, "Failed to open pipe for tar command\n");
        return;
    }

    // Write each file path to the tar command
    char *token = strtok(filepaths, "\n");
    while (token != NULL) {
        fprintf(f, "%s\n", token);  // give file paths one by one to tar file
        token = strtok(NULL, "\n");
    }

    int status = pclose(f);
    if (status == -1) {
        fprintf(stderr, "Failed to close pipe for tar command\n");
        return;
    }

    if (WIFEXITED(status)) {
        int exit_status = WEXITSTATUS(status);
        if (exit_status != 0) {
            fprintf(stderr, "Tar command exited with status %d\n", exit_status);
        }
    } else {
        fprintf(stderr, "Tar command did not terminate normally\n");
    }
    
}

// function to send tar file to client
int sendTarFile(int clt){
    char tar_name[20];
    sprintf(tar_name, "%d", clt);
    strcat(tar_name, "stemp.tar.gz");  // tar file name
    
    FILE *file = fopen(tar_name, "rb");  // open tar file
    if (file == NULL) {
        fprintf(stderr, "Failed to open tar file for reading\n");
        return -1;
    }
    
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);  // get file size using fseek
    rewind(file);
    write(clt, &file_size, sizeof(long)); // send file size to client

    char buffer[1024];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        write(clt, buffer, bytes_read);  // send content of tar to client
    }

    fclose(file);  // close tar
    remove(tar_name);  // delete temp tar file
    printf("Tar File Sent.\n");
}

// function to get creation time of given file path
char *getCreationTime(const char *filename){
    char cmd[100];
    snprintf(cmd, sizeof(cmd), "stat --printf=\"%w\\n\" \"%s\"", filename);  // stat command in cmd
    FILE *f = popen(cmd, "r"); // run stat
    char *op = (char *)malloc(50 * sizeof(char));
    fgets(op, 20, f);  // store output in op
    pclose(f);

    // Remove the newline character at the end, if present
    char *newline_pos = strchr(op, '\n');
    if (newline_pos != NULL) {
        *newline_pos = '\0';
    }

    return op;  // return date in string format
}

// function to get file extension of given file path
const char *getFileExt(const char *path){
    const char *ext = strrchr(path, '.');  // get all the characters after last '.' 
    if(ext && ext != path && *(ext + 1)){
        return ext+1; // return found extension
    }
    return "";  // "" if no extension
}

// find matching file paths to store in tar 
int getFileTar(const char *path, const struct stat *sb, int type, struct FTW *ftwbuf){

    // exclude these directories because some file does not have read permissions
    const char *directories_to_skip[] = {
        "/.cache",
        "/.chrom",
        "/.mozilla",
        "/.config",
        "/CacheStorage",
        "/.lesshst",
        "/.local",
        "/.var",
        "/.gnupg",
        "/.sudo_as_admin_successful",
        "/.gnome",
        "/.dotnet"
    };

    // Check if the current directory matches any directories to skip
    for (size_t i = 0; i < sizeof(directories_to_skip) / sizeof(directories_to_skip[0]); ++i) {
        if (strstr(path, directories_to_skip[i])) {
            return 0; // Skip the entire subtree
        }
    }
    
    // if found file is file then go inside if
    if (type == FTW_F){
        if (fileExists(path)){  // check file exists or not
            if (strcmp(args[0], "getfz") == 0){  // for getfz command
                int size = getFileSize(path);  // get size of file
                if (size != -1){
                    if (size >= size1 && size <= size2){  // if size is between given sizes then add this file path to filepaths array separated by \n 

                        if (strlen(filepaths) == 0){  // if adding for first time then only add path not \n
                            strcat(filepaths, path);
                        }
                        else{
                            strcat(filepaths, "\n");  // separator
                            strcat(filepaths, path);
                        }
                    }
                }
            }
            else if (strcmp(args[0], "getft") == 0){  // for getft command
                const char *ext = getFileExt(path);  // get extension of file
                
                // if it matches with given extension list then add to filepaths array same ad getfz
                if (ext != "" && (strcmp(ext, args[1]) == 0 || (args[2] != NULL && strcmp(ext, args[2]) == 0) || (args[3] != NULL && strcmp(ext, args[3]) == 0))){
                    if (strlen(filepaths) == 0){
                            strcat(filepaths, path);
                    }
                    else{
                        strcat(filepaths, "\n");
                        strcat(filepaths, path);
                    }
                }
            }
            if (strcmp(args[0], "getfdb") == 0){  // for getfdb command
                char *cd = getCreationTime(path);  // get creation time of file
                int dr = strcmp(cd, firstarg);  // compare file creation time and input time
                if (dr < 0 || dr == 0){  // if file creation time is before or equal to given time then add path to filepaths array same as above two commands
                    if (strlen(filepaths) == 0){
                        strcat(filepaths, path);
                    }
                    else{
                        strcat(filepaths, "\n");
                        strcat(filepaths, path);
                    }
                }
            }
            if (strcmp(args[0], "getfda") == 0){  // for getfda command
                char *cd = getCreationTime(path);  // get creation time of file
                int dr = strcmp(cd, firstarg);  // compare file creation time and input time
                if (dr > 0 || dr == 0){  // if file creation time is after or equal to given time then add path to filepaths array same as above three commands
                    if (strlen(filepaths) == 0){
                        strcat(filepaths, path);
                    }
                    else{
                        strcat(filepaths, "\n");
                        strcat(filepaths, path);
                    }
                }
            }
        }
    }
    
    
    
    return 0;
}

// function for getfn command
int getFileInfo(const char *path, const struct stat *sb, int type, struct FTW *ftwbuf){
    char *found_filename = strrchr(path, '/');  // get file name from the path
    
    if (type == FTW_F && strcmp(++found_filename, firstarg) == 0){  // compare filename with input filenaem
        int size = getFileSize(path);  // get size of file
        if (size != -1){
          char *creationTime = getCreationTime(path); // creation time of file
          char *permission = getPermission(path);  // permissions of file
          snprintf(r_value_fn, sizeof(r_value_fn), "Filename: %s\nSize: %d\nPermission: %s\nCreation Date: %s\n", firstarg, size, permission, creationTime);  // add all these things in r_value_fn array
          return 1;
        }
    }
    return 0;
}

// function as described in requirements
int pclientrequest(int clt){
    for (;;){  // infinite loop to get commands from users
        char b[100];
        read(clt, b, 100);  // read command
        printf("Received: %s\n", b);

        if (strcmp(b, "quitc") == 0){  // for quitc option
            close(clt);  // close connection
            kill(getpid(), SIGTERM);  // kill process
        }
        
        // empty args array
        for (int i = 0; i < 4; i++) {
            args[i] = NULL;
        }
        argc = 0;
        argc = getArgs(b, args);  // store arguments in args and argc
        
        if (strcmp(args[0], "getfn") == 0){  // for getfn
            firstarg = args[1];
            nftw(source, getFileInfo, 10, FTW_PHYS);  // travers through directory tree
            if (strlen(r_value_fn) != 0){
                write(clt, r_value_fn, sizeof(r_value_fn));  // if r_value_fn is not empty then return it too client
                printf("File Information Sent to Client\n");
            }
            else{
                char m[1] = "n";  // send 'n' if file not found
                write(clt, m, sizeof(m));
                memset(m, '\0', sizeof(m));
            }
            firstarg = "";  // empty global variables
            r_value_fn[0] = '\0';  // empty global variables
        }
        else if (strcmp(args[0], "getfz") == 0){  // for getfz
            size1 = atoi(args[1]);  // bottom limit
            size2 = atoi(args[2]);  // top limit
            nftw(source, getFileTar, 10, FTW_PHYS);  // traversal
            if (strlen(filepaths) != 0){  // if filepaths is not empty
                createTar(clt);  // create tar file
                sendTarFile(clt);  // send that tar file to client
            }
            else{
                off_t file_size = -1;  // else send tar file size = -1
                write(clt, &file_size, sizeof(off_t));
            }
            size1 = -1;  // reset variables
            size2 = -1;  // reset variables
            filepaths[0] = '\0';  // reset variables
        }
        else if (strcmp(args[0], "getft") == 0){  // for getft same as getfz find filenames -> create tar -> send tar to client and if files not found then send size = -1
            nftw(source, getFileTar, 10, FTW_PHYS);
            if (strlen(filepaths) != 0){
                createTar(clt);
                sendTarFile(clt);
            }
            else{
                off_t file_size = -1;
                write(clt, &file_size, sizeof(off_t));
            }
            filepaths[0] = '\0';
        }
        else if (strcmp(args[0], "getfdb") == 0){  // same as getft and getfz
            firstarg = args[1];  // date
            nftw(source, getFileTar, 10, FTW_PHYS);
            if (strlen(filepaths) != 0){
                createTar(clt);
                sendTarFile(clt);
            }
            else{
                off_t file_size = -1;
                write(clt, &file_size, sizeof(off_t));
            }
            firstarg = "";
            filepaths[0] = '\0';
        }
        else if (strcmp(args[0], "getfda") == 0){  // same as above options
            firstarg = args[1];  // date
            nftw(source, getFileTar, 10, FTW_PHYS);
            if (strlen(filepaths) != 0){
                createTar(clt);
                sendTarFile(clt);
            }
            else{
                off_t file_size = -1;
                write(clt, &file_size, sizeof(off_t));
            }
            firstarg = "";
            filepaths[0] = '\0';
        }
        memset(b, '\0', sizeof(b));
    }
}

int main(){
    int clt;
    struct sockaddr_in address;
    
    char *buf;
    buf=(char *)malloc(10*sizeof(char));
    buf=getlogin();
    strcat(source, buf);  // add username to root path
    
    skt = socket(AF_INET, SOCK_STREAM, 0);  // create socket
    
    if (skt == -1){
        printf("Could not Created Socket!\n");
        exit(0);
    }
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    address.sin_port = htons(PORT);
    int opt = 1;
    setsockopt(skt, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));  // it will bind socket even if port is not free
    
    // bind socket
    bind(skt, (struct sockaddr*) &address, sizeof(address));
    
    // listen max 10
    listen(skt, 10);
    printf("Server Started....\n");
    
    // signal handler
    signal(SIGINT, signalHandler);
    
    // clients connection count. We will send this value to clients
    int clients_count = 1;
    
    while (1){
        clt = accept(skt, (struct sockaddr*)NULL, NULL);  // accept connection
        
        write(clt, &clients_count, sizeof(clients_count));  // send clients_count
        
        int rtn;
        read(clt, &rtn, 4);  // value for server client or mirror client, 1 == mirror client, 0 == server client
        
        clients_count++;  // increase counter
        
        if (rtn == 0 && fork() == 0){  // if server count then only fork process and call pclientrequest function
            printf("Client Connected.\n");
            pclientrequest(clt);
        }
    }
    
    return 0;
}
