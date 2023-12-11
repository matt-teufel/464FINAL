#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "json-server.h"
#include "smartalloc.h"

const char * GET = "GET";
const char * QUIT = "/json/quit";
const char * TERM = "\r\n\r\n";
struct hash_table * ht;
struct client_node * client_list;
pthread_mutex_t client_lock = PTHREAD_MUTEX_INITIALIZER;

void create_client_list() {
    client_list = malloc(sizeof(struct client_node));
    client_list->next = client_list;
    client_list->prev = client_list;
    client_list->data = NULL;
}

void add_client(struct client_node * client) {
    pthread_mutex_lock(&client_lock);
    client->prev = client_list->prev;
    client_list->prev->next = client;
    client->next = client_list;
    pthread_mutex_unlock(&client_lock);
}

void remove_client(struct client_node * client) { 
    pthread_mutex_lock(&client_lock);
    client->prev->next = client->next;
    client->next->prev = client->prev;
    if(client->data->buff != NULL) {
        free(client->data->buff);
    }
    free(client->data);
    free(client);
    pthread_mutex_unlock(&client_lock);
}

void free_client_list() { 
    struct client_node * current_client = client_list->next;
    struct client_node * temp;
    //remove all list elements 
    while(current_client->data != NULL) { 
        temp = current_client->next;
        remove_client(current_client);
        current_client = temp;
    }
    //free our head last 
    free(client_list);
}
void create_hash_table() {
    ht = malloc(sizeof(struct hash_table));
    ht->entries = calloc(HASHTABLE_SIZE, sizeof(struct hash_node *));
    add_entry("/json/implemented.json", "json/implemented.json", "application/json", 
    "[{\"feature\": \"about\", \"URL\": \"/json/about\"}, {\"feature\": \"quit\", \"URL\": \"/json/quit\"}]");
    add_entry("/json/quit", "json/quit.json", "application/json", 
    "{\"result\": \"success\"}");
    add_entry("/json/about", "json/about.json", "application/json", 
    "{\"author\": \"Matthew Teufel\", \"email\": \"mteufel@calpoly.edu\", \"major\": \"CPE\"}");
}

void add_entry(char * key, char * file_name, char * mime, char * content){
    uint32_t hash = hashing_func(key);
    struct hash_node * new_node = calloc(1, sizeof(struct hash_node));
    new_node->key = key;
    new_node->file_name = file_name;
    new_node->mime = mime;
    new_node->content = content;
    new_node->next = NULL;
    struct hash_node * current_node = ht->entries[hash];
    if(current_node == NULL) {
        ht->entries[hash] = new_node;
    } else {
        while(current_node->next != NULL){
            current_node = current_node->next;
        }
        current_node->next = new_node;
    }
}

struct hash_node * get_entry(char * key) {
    uint32_t hash = hashing_func(key);
    struct hash_node * current_node = ht->entries[hash];
    while(current_node != NULL){
        if(strcmp(current_node->key, key) == 0) {
            return current_node;
        }
        current_node = current_node->next;
    }
    return NULL;
}

//random simple hashing funciton 
uint32_t hashing_func(char * key){
    int hash = 4321;
    char * letter;
    letter = key;
    while(*letter != '\0') {
        hash += (*letter << 7);
        letter++;
    }
    return hash % HASHTABLE_SIZE;
}

void free_hashtable() { 
    int i;
    struct hash_node * current_node;
    struct hash_node * temp;
    for (i = 0; i < HASHTABLE_SIZE; i++) {
        current_node = ht->entries[i];
        while(current_node != NULL){
            temp = current_node->next;
            free(current_node);
            current_node = temp;
        }
    }
    free(ht->entries);
    free(ht);
}

void signal_handler(int signum) { 
    if(signum == SIGINT){
        free_hashtable();
        free_client_list();
        printf("Server exiting cleanly.\n");
        fflush(stdout);
        exit(0);
    }
}

void * handle_client (void * arg) { 
    // printf("handling the client \n");
    int byte_count = 1;
    struct client_node * client = (struct client_node *)arg;
    struct hash_node * file_node;
    char * token;
    int offset = 0;
    if((byte_count = recv(client->data->fd, client->data->buff + offset, BUFFSIZE-offset, 0)) == -1) {
        perror("error reading client input");
        exit(-1);
    }
    offset += byte_count;
    if(strncmp(client->data->buff + offset - strlen(TERM), TERM, strlen(TERM)) != 0){
        while(strncmp(client->data->buff + offset - strlen(TERM), TERM, strlen(TERM)) != 0) { 
            if((byte_count = recv(client->data->fd, client->data->buff + offset, BUFFSIZE-offset, 0)) == -1) {
                perror("error reading client input");
                exit(-1);
            }
            offset += byte_count;
        }
    }
    token = strtok(client->data->buff, " ");
    if(strncmp(GET, token, strlen(GET)) == 0) {
        // printf("this is a get\n");
        token = strtok(NULL, " ");
        file_node = get_entry(token);
        // printf("file name %s", file_node->file_name);
        char * response = create_return_string(file_node);
        send(client->data->fd, response, strlen(response), 0);
        free(response);
    }
    close(client->data->fd);
    if(strncmp(QUIT, token, strlen(QUIT)) == 0){
        raise(SIGINT);
    }
    remove_client(client);
    return NULL;
}

char * create_return_string(struct hash_node * file_node) {
    char * return_string = malloc(BUFFSIZE);
    int file_size = strlen(file_node->content);
    sprintf(return_string,
        "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %d\r\n\r\n%s\r\n%c", 
        file_node->mime,
        file_size, 
        file_node->content,
        '\0');
    return return_string;
}

int main(int argc, char * argv[]) {
    // set up signal handler for freeing and exitting 
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigfillset(&sa.sa_mask);
	sa.sa_flags = 0;
	if (-1 == sigaction(SIGINT, &sa, NULL))
	{
		perror("Couldn't set signal handler for SIGINT");
		return 2;
	}

    struct sockaddr_in server_addr, client_addr; 
    int server;
    socklen_t client_addr_len = sizeof(client_addr);
    socklen_t server_addr_len;
    pthread_t tid;
    uint16_t port;
    struct client_node * client_n;
    int fd;

    //create file system hash table
    create_hash_table();

    //create data strucutre for keeping track of clients 
    create_client_list();

    // create sock 
    if((server = socket(AF_INET, SOCK_STREAM, 0))  == -1){
        perror("error creating the socket");
        exit(-1);
    }
    int optval = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    //configure and bind 
    if(argc > 1){
        server_addr.sin_addr.s_addr = inet_addr(argv[1]);
    } else {
        server_addr.sin_addr.s_addr = INADDR_ANY;
    }
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = 0; //dynamic ports 
    if (bind(server, (struct sockaddr *)(&server_addr), sizeof(server_addr)) == -1) {
        perror("unable to bind socket to address");
        exit(-1);
    }

    server_addr_len = sizeof(server_addr);
    if(getsockname(server, (struct sockaddr *)(&server_addr), &server_addr_len) == -1){
        perror("unable to get sock number");
        exit(-1);
    }
    port = ntohs(server_addr.sin_port);
    printf("HTTP server is using TCP port %u\n", port);
    printf("HTTPS server is using TCP port -1\n");
    fflush(stdout);


    //listen to max connections
    listen(server, SOMAXCONN);
    while(1) { 
        fd = accept(server, (struct sockaddr *)(&client_addr), &client_addr_len);
        if(fd == -1){
            perror("error accepting client");
            exit(-1);
        }
        client_n = malloc(sizeof(struct client_node));
        client_n->data = malloc(sizeof(struct client_data));
        client_n->data->fd = fd;
        client_n->data->buff = malloc(BUFFSIZE);
        add_client(client_n);
        pthread_create(&tid, NULL, handle_client, (void*)(client_n));
        pthread_detach(tid);
    }

}