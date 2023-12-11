#include <pthread.h>

#define BUFFSIZE 2097152
#define HASHTABLE_SIZE 1024

struct client_data {
    int fd;
    char * buff;
} __attribute__((packed));

struct client_node { 
    struct client_data * data;
    struct client_node * next;
    struct client_node * prev;
} __attribute__((packed));


struct hash_node {
    char * key;
    char * file_name;
    char * content;
    char * mime;
    struct hash_node * next;
} __attribute__((packed));

struct hash_table {
    struct hash_node ** entries;
} __attribute__((packed));

void create_hash_table();
void add_entry(char * key, char * file_name, char * mime, char * content);
uint32_t hashing_func(char * key);
struct hash_node * get_entry(char * key); 
char * create_return_string(struct hash_node * file_node);