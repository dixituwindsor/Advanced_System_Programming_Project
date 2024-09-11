#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include <ctype.h>
#define PORT_SERVER 4000  // port for server
#define PORT_MIRROR 4001  // port for mirror

int srv;  // file discriptor of socket
char source[50] = "/home/";  // path to root directory ~

// to handle ctrl+c signal (SIGINT)
void signalHandler(int sig){
    char buf[5] = "quitc";
    write(srv, buf, strlen(buf));  // send quitc command to server when pressed ctrl+c
    close(srv);  // close connection
    exit(sig);  // exit process
}

// this function will check that entered string in getfdb and getfda is date or not
int isDate(const char *date) {

    if (strlen(date) != 10)
        return 0;

    for (int i = 0; i < 10; ++i) {
        if (i == 4 || i == 7) {
            if (date[i] != '-')
                return 0;
        } else {
            if (!isdigit(date[i]))
                return 0;
        }
    }

    return 1;
}

// this function will check if the entered command is valid or not
int isCommandValid(char *cmd, char *args[]){
    char *token = strtok(cmd, " ");  // tokenize input by space
    int argc = 0;  //total arguments in entered input including command name
    
    while (token != NULL){
        args[argc] = strdup(token);  // store arguments in array
        token = strtok(NULL, " ");
        argc++;
    }
    
    // this condition will check validity of entered input with all the available commands
    if ((strcmp(args[0], "quitc") == 0 && argc == 1) || (strcmp(args[0], "getfn") == 0 && argc == 2) || (strcmp(args[0], "getfz") == 0 && argc == 3 && atoi(args[1]) >= 0 && atoi(args[2]) >= 0 && atoi(args[1]) <= atoi(args[2])) || (strcmp(args[0], "getft") == 0 && argc >=2 && argc <= 4) || (strcmp(args[0], "getfdb") == 0 && argc == 2 && isDate(args[1]) == 1) || (strcmp(args[0], "getfda") == 0 && argc == 2 && isDate(args[1]) == 1)){
        return 1;  // command is valid
    }
    else{
      return 0;  // command is invalid
    }
}

int main(int argc, char *argv[]){
    int connfd;
    struct sockaddr_in address_server;  // for server
    struct sockaddr_in address_mirror;  // for mirror
    
    char *buf;
    buf=(char *)malloc(10*sizeof(char));
    buf=getlogin();
    strcat(source, buf);  // add username in path
    strcat(source, "/f23project");  // path to store tar file /home/username/f23project
    struct stat st = {0};
    
    // create f23project if not exists
    if (stat(source, &st) == -1) {
        mkdir(source, 0777);
    }
    
    // signal handler
    signal(SIGINT, signalHandler);
    
    strcat(source, "/temp.tar.gz");  // add tar filename in path
    srv = socket(AF_INET, SOCK_STREAM, 0);  // create socket
    
    if (srv == -1){
        printf("Socket not Created Successfully!\n");
    }
   
    address_server.sin_family = AF_INET;
    address_server.sin_addr.s_addr = inet_addr("127.0.0.1");  // local ip
    address_server.sin_port = htons(PORT_SERVER);  // port
   
    //connect with server using ip and port
    if (connect(srv, (struct sockaddr*)&address_server, sizeof(address_server)) != 0){
        printf("Connection to Server Failed. Now Connecting to Mirror\n");
    }

    int b = -1;  // by default b = -1
    read(srv, &b, 4);  // server will send client number
    
    // for connection to server (if the number (b) which server has sent is between 0 to 4 or 9, 11, 13 then it means this client will be handle by server)
    if ((b != -1) && ((b > 0 && b <= 4) || (b >=9 && b%2!=0))){
        int return_value = 0;  // send 0 to server for server client
        write(srv, &return_value, sizeof(return_value));  // send 0 and stay connected to server
    }
    else{  // for connection to mirror clients
        int return_value = 1;  // send 1 to server for mirror client
        write(srv, &return_value, sizeof(return_value)); // send 1
        close(srv);  // disconect from server

        // connect to mirror
        srv = socket(AF_INET, SOCK_STREAM, 0);
    
        if (srv == -1){
            printf("Mirror Socket not Created Successfully!\n");
        }
       
        address_mirror.sin_family = AF_INET;
        address_mirror.sin_addr.s_addr = inet_addr("127.0.0.1");
        address_mirror.sin_port = htons(PORT_MIRROR);
       
        if (connect(srv, (struct sockaddr*)&address_mirror, sizeof(address_mirror)) != 0){
            printf("Connection to Server Failed!\n");
            exit(0);
        }
    }
    
    // get command inputs from user
    while (1){
        char cmd[100] = {};
        printf("Write command for server: ");
        
        if (scanf("%[^\n]", cmd) == 1){    
            int c;
            while ((c = getchar()) != '\n' && c != EOF);
            char buf[100];
            strcpy(buf, cmd);

            char *args[4] = {NULL};
            
            if (!isCommandValid(cmd, args)){  // if command is invalid
                printf("Command can be one of below:\n1) getfn filename\n2) getfz size1 size2 (size1 <= size2, size1>=0, size2>=0\n3) getft <extension list> upto 3 extensions only\n4) getfdb date (yyyy-mm-dd)\n5) getfda date (yyyy-mm-dd)\n6) quitc (to quit)\n");
            }
            else{
                write(srv, buf, strlen(buf) + 1);  // send command to server or mirror
                char b[1000];
                if (strcmp(args[0], "quitc") == 0){  // if quitc then break the look and program will exit
                    break;
                }
                else if (strcmp(args[0], "getfn") == 0){  // for getfn command
                    read(srv, b, 1000);  // get value from the server or mirror
                    
                    if (b[0] == 'n'){  // if value is n then file not found
                        printf("File not found\n");
                    }
                    else{
                        printf("%s", b);  // printf file information
                    }
                    memset(b, '\0', sizeof(b));  // empty buffer
                }
                else if(strcmp(args[0], "getfz") == 0 || strcmp(args[0], "getft") == 0 || strcmp(args[0], "getfdb") == 0 || strcmp(args[0], "getfda") == 0){  // for commands that receives tar file
                    off_t file_size;
                    read(srv, &file_size, sizeof(off_t));  // get size of tar file
                    
                    // size = -1 means no file found.
                    if ((int)file_size != -1){
                        int file = open(source, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);  // create and open a tar file
                        if (file < 0) {
                            perror("File opening failed");
                            return EXIT_FAILURE;
                        }

                        // get the tar file data from the server and store it in file
                        char buffer[1024];
                        ssize_t total_bytes_received = 0;
                        ssize_t bytes_received;
                        while (total_bytes_received < file_size) {
                            bytes_received = read(srv, buffer, sizeof(buffer));
                            write(file, buffer, bytes_received);  // write received data in tar file
                            total_bytes_received += bytes_received;
                        }

                        close(file);
                        printf("Tar File Saved.\n");
                    }
                    else{
                        printf("No file found\n");
                    }
                    
                }
                
            }
        }
    }

    close(srv); // close connection to server or mirror
    return 0;
}
