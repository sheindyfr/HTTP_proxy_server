#include "207191131_threadpool.h"
#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <netdb.h>

#define REQUEST_LEN 500
#define RESPONSE_LEN 500

void connect_to_client(int* server_fd, int port);
void connect_to_server(int *serv_fd, int port, char* host);
char** read_filter_hosts(char* file_name);
void destroy_filter();
void err_sys_calls(char*, unsigned char*, char*, int);
int do_request(void *);
int check_request(char req[], char** host, int *port);
unsigned char* error_response(int type);
int check_host_vs_filter(char*);
void destroy_arr(char* arr[], int size);

/*----------------------------------------*/
char **filter_hosts;
int num_hosts;

/*------------error functions-------------*/
void err(char* msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}
void usage_error_proxy() {
    printf("Usage: threadpool <pool-size> <max-number-of-jobs>\n");
    exit(EXIT_FAILURE);
}
void err_sys_calls(char* host, unsigned char* res, char* msg, int fd){
    destroy_filter();
    if(!fd)
        close(fd);
    if(write(fd, res, strlen((char*)res)+1)<0) { //send the response to client
    }
    memset(res, 0, sizeof((char*)res));
    close(fd);
    if(host){
        host=NULL;
        free(host);
    }
    if(res){
        res=NULL;
        free(res);
    }
}
/*----------------------------------------*/
int main(int argc, char* const argv[])
{
    int new_socket, i;
    int server_fd;
    threadpool *pool;
    if(argc!=5)
        usage_error_proxy();
    int port=atoi(argv[1]);
    int pool_size=atoi(argv[2]);
    int max_num_of_threads=atoi(argv[3]);
    if(!port || !pool_size || !max_num_of_threads)
        usage_error_proxy();
    filter_hosts=read_filter_hosts(argv[4]);

    /*-----------------connection-----------------*/
    connect_to_client(&server_fd, port);

    pool=create_threadpool(pool_size);

    /*----------------start handling the request----------------*/
    for(i=0; i<max_num_of_threads; i++)
    {
        if ((new_socket = accept(server_fd, NULL, NULL))<0)
        {
            destroy_filter(num_hosts);
            err("ERROR accept");
        }
        dispatch(pool, do_request, &new_socket);
    }
    destroy_threadpool(pool);
    close(server_fd);
}//close main

/**
 * connect to client
 * @param server_fd the proxy fd
 * @param port logical port
 */
void connect_to_client(int* server_fd, int port)
{
    struct sockaddr_in address;
    // Creating socket file descriptor
    if ((*server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        destroy_filter();
        err("ERROR opening socket");
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    // Forcefully attaching socket to the port
    if (bind(*server_fd, (struct sockaddr *)&address, sizeof(address))<0)
    {
        destroy_filter();
        err("ERROR on binding");
    }
    if (listen(*server_fd, 3) < 0)
    {
        destroy_filter();
        err("ERROR on listening");
    }
}
/**
 * connect to remote server
 * @param serv_fd the proxy fd
 * @param port logical port of remote server
 * @param host remote server
 */
void connect_to_server(int *serv_fd, int port, char* host) {
    struct sockaddr_in serv_addr;
    struct hostent *server;
    if ((*serv_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        err_sys_calls(host, NULL, "ERROR open socket", *serv_fd);
    }
    server = gethostbyname(host);
    if (server == NULL) {
        free(host);
        destroy_filter();
        fprintf(stderr, "ERROR, no such host\n");
        exit(EXIT_FAILURE);
    }
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr = *((struct in_addr *) server->h_addr);
    serv_addr.sin_port = htons(port);
    memset(&(serv_addr.sin_zero), '\0', 8);

    if (connect(*serv_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        err_sys_calls(host, NULL, "ERROR connect socket", *serv_fd);
    }
}
/**
 * read the filter file to array
 * @param file_name the path of filter file
 * @return array of hosts
 */
char** read_filter_hosts(char* file_name)
{
    FILE * fp;
    char * line = NULL;
    char **hosts_arr=NULL;
    size_t len = 0;
    int arr_len=0, i=0;
    ssize_t read;

    fp = fopen(file_name, "r");
    if (fp == NULL)
    {
        err("ERROR open file");
    }
    //count the number of hosts in the file
    while ((read = getline(&line, &len, fp)) != -1)
    {
        arr_len++;
        if(line)
        {
            free(line);
            line=NULL;
        }
    }
    hosts_arr=malloc(arr_len* sizeof(char*));
    if(hosts_arr==NULL)
    {
        fclose(fp);
        err("ERROR malloc");
    }
    if(line)
    {
        free(line);
        line=NULL;
    }
    rewind(fp);
    //fill the array
    while ((read = getline(&line, &len, fp)) != -1)
    {
        if(line==NULL)
            break;
        hosts_arr[i]=malloc(strlen(line)* sizeof(char)+1);
        if(hosts_arr[i]==NULL)
        {
            fclose(fp);
            destroy_filter();
            free(line);
            err("ERROR malloc");
        }
        strcpy(hosts_arr[i], line);
        hosts_arr[i][strlen(hosts_arr[i])-1]='\0';
        i++;
        if (line)
        {
            free(line);
            line=NULL;
        }
    }
    if(line)
    {
        free(line);
        line=NULL;
    }
    num_hosts=arr_len;
    fclose(fp);

    return hosts_arr;
}
/**
 * free the hosts array
 */
void destroy_filter()
{
    int i;
    for(i=0; i<num_hosts; i++)
        if(filter_hosts[i])
            free(filter_hosts[i]);
    if(filter_hosts)
        free(filter_hosts);
}
/**
 * function of threads, get the request and write the response
 * @param new_client_fd socket of client
 * @return 0 if OK
 */
int do_request(void *new_client_fd)
{
    int client_fd=*((int*)(new_client_fd));
    int serv_fd, serv_port;
    int valread=-1, code;
    char req_tmp[REQUEST_LEN];
    char req[REQUEST_LEN];
    char* host;
    unsigned char *res=NULL;

    /*
     * reading the request from client
     */
    memset(req, '\0', sizeof(req));
    memset(req_tmp, '\0', sizeof(req_tmp));
    while((valread=read(client_fd, req_tmp, REQUEST_LEN-1))>0) { //read the request from client
        req_tmp[strlen(req_tmp)-2]='\0'; //delete the \r\n
        if(req_tmp[0]=='\0' && req[0]!= '\0') //check if the request is finished
        {
            memset(req_tmp, '\0', sizeof(req_tmp));
            strcat(req, "\r\n");
            break;
        }
        strcat(req, req_tmp);
        strcat(req, "\r\n");
        memset(req_tmp, '\0', sizeof(req_tmp)); //zero the buff
    }
    if(valread<0)
    {
        close(client_fd);
        destroy_filter();
        err("ERROR read");
    }
    /*
     * check the valid of request
     */
    code=check_request(req, &host, &serv_port); //check 400 or 501 response
    if(code<0)
    {
        res=error_response(500); //if system call is failed during the handle
        err_sys_calls(host, res, "ERROR malloc", client_fd);
    }
    if(!code)
        code=check_host_vs_filter(host); //check if the host is legal vs the filter file
    if(code) {
        res=error_response(code);
        if(write(client_fd, res, strlen((char*)res)+1)<0) { //send the response to client
            free(res);
            res=error_response(500);
            err_sys_calls(host, res, "ERROR write", client_fd);
        }
        memset(res, 0, sizeof((char*)res));
        memset(req, 0, sizeof(req));
        free(res);
        close(client_fd);
        return 0;
    }
    /*
     * writing the request to far host
     */
    connect_to_server(&serv_fd, serv_port, host);
    if(write(serv_fd, req, strlen(req)+1)<0)  //send the request to server
    {
        close(serv_fd);
        res=error_response(500);
        err_sys_calls(host, res, "ERROR write", client_fd);
    }
    memset(req, 0, strlen(req));
    res=malloc(RESPONSE_LEN);
    if(res==NULL)
    {
        close(serv_fd);
        res=error_response(500);
        err_sys_calls(host, res, "ERROR malloc", client_fd);
    }
    while((valread=read(serv_fd, res, RESPONSE_LEN-1))>0) //read the response from far host
    {
        res[strlen((char*)res)]='\0';
        if(write(client_fd, res, valread)<0) //send the response to client
        {
            close(serv_fd);
            free(res);
            res=error_response(500);
            err_sys_calls(host, res, "ERROR write", client_fd);
        }
        memset(res, '\0', sizeof((char*)res)); //zero the buff
    }
    if(valread<0) {
        close(serv_fd);
        free(res);
        res=error_response(500);
        err_sys_calls(host, res, "ERROR read", client_fd);
    }
    if(host)
    {
        host=NULL;
        free(host);
    }

    if(res)
        free(res);
    //res=NULL;
    close(client_fd);
    close(serv_fd);
    return 0;
}
/**
 * check the validation of request
 * @param req the request from client
 * @param host pointer to host, in order to fill
 * @param port pointer to port, in order to fill
 * @return return the code of error response, else return 0
 */
int check_request(char req[], char** host, int *port)
{
    char *split, *first_line, *tmp_host, *check, *p;
    char tmp_req[REQUEST_LEN];
    char* words_arr[3];
    int i=0, index;
    *host=NULL;
    struct hostent *server;
    memset(words_arr, '\0', sizeof(words_arr));
    strcpy(tmp_req, req);
    /*------check if the request is legal
     *      check the first line of request------*/

    first_line=strtok(tmp_req, "\n"); //take the first line of request
    if(strcmp(first_line, "\r")==0)  //if the first line is \n take the next line
        first_line=strtok(NULL, "\n");
    split=strtok(first_line, " "); //tae the first word of the first line
    while(split != NULL)
    {
        words_arr[i]=malloc(strlen(split)+1);
        if(words_arr[i]==NULL)
        {
            return -1;
        }
        strcpy(words_arr[i], split);
        words_arr[i][strlen(words_arr[i])]='\0';
        split[0]='\0';
        split=strtok(NULL," ");
        i++;
        if((i>2 && split!=NULL) || (i<=2 && split==NULL)) //there are less or more than 3 words in the first line
        {
            if(split)
                if(strcmp(split, "\r")==0)
                    break;
            destroy_arr(words_arr, 3);
            return 400;
        }
    }
    if(strcmp(words_arr[0], "GET")!=0) //check METHOD
    {
        destroy_arr(words_arr, 3);
        return 501;
    }
    if(words_arr[2][strlen(words_arr[i-1])-1]=='\r')
        words_arr[2][strlen(words_arr[i-1])-1]='\0';
    if(strcmp(words_arr[2], "HTTP/1.0")!=0 && strcmp(words_arr[2], "HTTP/1.1")!=0) //check HTTP version
    {
        destroy_arr(words_arr, 3);
        return 400;
    }
    /*-----check the host header-----*/
    strcpy(tmp_req, req);
    p = strtok(tmp_req, "\n");
    do {
        if(p && (tmp_host=strstr(p, "Host: "))!=NULL) //check the host header
        {
            *host=malloc(strlen(p)+1);
            if(*host==NULL)
            {
                destroy_arr(words_arr, 3);
                return -1;
            }
            strcpy(*host, p);
            host[0][strlen(host[0])-1]='\0';
            *host=strtok(*host," ");
            if(strcmp(*host, "Host:")!=0)
            {
                destroy_arr(words_arr, 3);
                return 400;
            }
            *host=strtok(NULL," "); //take the word after 'Host:'
            break;
        }
    } while ((p = strtok(NULL, "\n")) != NULL);

    if(*host==NULL)
    {
        destroy_arr(words_arr, 3);
        return 400;
    }
    /*-------check the port-------*/
    else{
        check=strchr(*host, ':'); //check if we have port
        if(check!=NULL)
        {
            check=check+1;
            if(atoi(check)==0) //the port is not a number
            {
                *host=NULL;
                free(*host);
                destroy_arr(words_arr, 3);
                return 404;
            }
            *port=atoi(check); //init the port request with the check
            index=(int)strlen(*host)-(int)strlen(check)-1;
            host[0][index]='\0'; //leave the host without the :port
        }
        else
            *port=80; //the default port
    }
    server=gethostbyname(*host);
    if(server==NULL)
    {
        *host=NULL;
        free(*host);
        destroy_arr(words_arr, 3);
        return 404;
    }
    destroy_arr(words_arr, 3);
    return 0;
}
/**
 * build the error response
 * @param type type of error
 * @return pointer to response
 */
unsigned char* error_response(int type)
{
    char *res = malloc(RESPONSE_LEN);
    if(res==NULL)
    {
        destroy_filter();
        err("ERROR malloc");
    }
    char exp1[30];
    char exp2[30];
    char len[4];

    if (type == 400) {
        strcpy(exp1, "400 Bad Request");
        strcpy(exp2, "Bad Request.");
        strcpy(len, "113");
    }
    if (type == 403) {
        strcpy(exp1, "403 Forbidden");
        strcpy(exp2, "Access denied.");
        strcpy(len, "111");
    }
    if (type == 404) {
        strcpy(exp1, "404 Not Found");
        strcpy(exp2, "File not found.");
        strcpy(len, "112");
    }
    if (type == 500) {
        strcpy(exp1, "500 Internal Server Error");
        strcpy(exp2, "Some server side error.");
        strcpy(len, "144");
    }
    if (type == 501) {
        strcpy(exp1, "501 Not supported");
        strcpy(exp2, "Method is not supported.");
        strcpy(len, "129");
    }
    strcpy(res, "HTTP/1.0 ");
    strcat(res, exp1);
    strcat(res, "\nServer: webserver/1.0\r\nContent-Type: text/html\r\nContent-Length: ");
    strcat(res, len);
    strcat(res, "\r\nConnection: close\r\n\r\n");
    strcat(res, "<HTML><HEAD><TITLE>");
    strcat(res, exp1);
    strcat(res, "</TITLE></HEAD>\n");
    strcat(res, "<BODY><H4>");
    strcat(res, exp1);
    strcat(res, "</H4>\n");
    strcat(res, exp2);
    strcat(res, "\n</BODY></HTML>\n");
    return (unsigned char*)res;
}
/**
 * check the host vs the filter array
 * @param host to check vs the array
 * @return code of error, else: 0
 */
int check_host_vs_filter(char* host)
{
    int i;
    for(i=0; i<num_hosts; i++)
    {
        if(strcmp(filter_hosts[i], host)==0)
            return 403;
    }
    return 0;
}
void destroy_arr(char** arr, int size)
{
    int i;
    for(i=0; i<size; i++)
    {
        if(arr[i])
            free(arr[i]);
    }
}