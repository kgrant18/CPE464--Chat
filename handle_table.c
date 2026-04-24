#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "handle_table.h"


#define INIT_NUM_HANDLES 15
#define REALLOC_HANDLES 15


handle_table *create_handle_table(void) {
    //malloc 
    handle_table *h_table = (handle_table *)malloc(sizeof(handle_table));
    if (h_table == NULL) {
        fprintf(stderr, "malloc failed\n");
        return NULL;
    }

    //create INIT_NUM_HANDLES entries initially
    h_table->entries = malloc(sizeof(handle_entry) * INIT_NUM_HANDLES);
    if (h_table->entries == NULL) {
        fprintf(stderr, "malloc failed for entry\n");
        free(h_table); 
        return NULL; 
    }

    //initialize size and capacity 
    h_table->size = 0; 
    h_table->capacity = INIT_NUM_HANDLES; 
    
    //populate empty space
    int i = 0; 
    for (i = 0; i < INIT_NUM_HANDLES; i++) {
        h_table->entries[i].handle_name = NULL;
        h_table->entries[i].socket_num = -1;
    }

    return h_table;  
}

int add_handle(handle_table *h_table, char *handle_name, int handle_num) {
    //NULL check
    if (h_table == NULL) {
        return -1; 
    }
    
    //socket_num should not be 0, 1, or 2
    if (handle_num == 0 || handle_num == 1 || handle_num == 2) {
        fprintf(stderr, "socket_num should not be 0, 1, or 2\n");
        return -1; 
    }

    //check if name is already being used
    if (lookup_name(h_table, handle_name) >= 0) {
        fprintf(stderr, "Handle arleady exists: %s\n", handle_name);
        return -1; 
    }

    //realloc if need more space 
    if (h_table->size >= h_table->capacity) {
        //double capacity count
        int old_capacity = h_table->capacity; 
        h_table->capacity *= 2; 
        
        handle_entry *realloc_ptr = realloc(h_table->entries, sizeof(handle_entry) * h_table->capacity); 
        if (realloc_ptr == NULL) {
            fprintf(stderr, "realloc failed\n");
            return -1;
        }
        else {
            //reassign pointer 
            h_table->entries = realloc_ptr;
        }

        int i = 0; 
        for (i = old_capacity ; i < h_table->capacity; i++ ) {
            //populate empty space
            h_table->entries[i].handle_name = NULL;
            h_table->entries[i].socket_num = -1; 
        }
    }

    //add new handle to handle table
    h_table->entries[h_table->size].handle_name = strdup(handle_name);
    if (h_table->entries[h_table->size].handle_name == NULL) {
        fprintf(stderr, "strdup failed\n");
        return -1; 
    }

    //assign handle_num and increment table size
    h_table->entries[h_table->size].socket_num = handle_num;    
    h_table->size++; 

    return 0; 
    
}

int remove_handle(handle_table *h_table, int handle_num) {
    //NULL check
    if (h_table == NULL) {
        return -1; 
    }

    int index = lookup_num(h_table, handle_num);
    if (index < 0) {
        return -1; 
    }

    //free handle from memory
    free(h_table->entries[index].handle_name);

    //remove the hole/fragmentation by shifting each entry left an index
    int i = 0;
    for (i = index; i < h_table->size - 1; i++) {
        h_table->entries[i] = h_table->entries[i + 1];
    }

    //decrement count 
    h_table->size--;

    //after shifting, top index should be reset
    h_table->entries[h_table->size].handle_name = NULL;
    h_table->entries[h_table->size].socket_num = -1; 

    return 0; 
}

int lookup_name(handle_table *h_table, char *name) {
    //NULL check
    if (h_table == NULL) {
        return -1; 
    }

    int i = 0;
    for (i = 0; i < h_table->size; i++) {
        if (strcmp(h_table->entries[i].handle_name, name) == 0) {
            //name found
            return i; 
        }
    }
    
    //name not found
    return -1; 
}

int lookup_num(handle_table *h_table, int socket_num) {
    //NULL check
    if (h_table == NULL) {
        return -1; 
    }

    int i = 0;
    for (i = 0; i < h_table->size; i++) {
        if (h_table->entries[i].socket_num == socket_num) {
            //num found
            return i; 
        }
    }
    //num not found
    return -1; 
}

int get_num_handles(handle_table *h_table) {
    return h_table->size;
}

char *get_handle_by_index(handle_table *h_table, int index) {
    return h_table->entries[index].handle_name;
}

int get_socket_by_index(handle_table *h_table, int index) {
    return h_table->entries[index].socket_num;
}

void print_handle_table(handle_table *h_table) {
    int i = 0; 
    for (i = 0; i < h_table->size; i++) {
        if (h_table->entries[i].socket_num != -1)
            printf("Handle %d: %s\n", h_table->entries[i].socket_num, h_table->entries[i].handle_name);
    }
}

void destroy_handle_table(handle_table *h_table) {
    //NULL check
    if (h_table == NULL) {
        return; 
    }

    int i = 0;
    for (i = 0; i < h_table->size; i++) {
        free(h_table->entries[i].handle_name);
    }

    free(h_table->entries);
    free(h_table); 
}

// int main(void) {
//     handle_table *table = create_handle_table();
//     if (table == NULL) {
//         fprintf(stderr, "failed to create table\n");
//         return 1;
//     }

//     add_handle(table, "alice", 4);
//     add_handle(table, "bob", 5);
//     add_handle(table, "charlie", 8);

//     print_handle_table(table);

//     int idx = lookup_name(table, "bob");
//     if (idx != -1) {
//         printf("Found bob at index %d, socket %d\n",
//                idx,
//                table->entries[idx].socket_num);
//     }

//     remove_handle(table, "alice");

//     printf("\nAfter removing alice:\n");
//     print_handle_table(table);

//     destroy_handle_table(table);
//     return 0;
// }