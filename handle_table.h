#ifndef __HANDLE_TABLE_H__
#define __HANDLE_TABLE_H__

typedef struct handle_entry {
    char *handle_name;
    int socket_num; 
} handle_entry;

typedef struct handle_table {
    handle_entry *entries; 
    int size; 
    int capacity; 
} handle_table;

handle_table *create_handle_table(void);
int add_handle(handle_table *h_table, char *handle_name, int handle_num);
int remove_handle(handle_table *h_table, int handle_num);
int lookup_name(handle_table *h_table, char *name);
int lookup_num(handle_table *h_table, int socket_num);
int get_num_handles(handle_table *h_table);
char *get_handle_by_index(handle_table *h_table, int index);
void print_handle_table(handle_table *h_table);
void destroy_handle_table(handle_table *h_table);

#endif //HANDLE_TABLE_H
