#ifndef CACHE_H
#define CACHE_H

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <libgen.h>
#include <sys/mman.h>
#include <semaphore.h>
#include  <sys/ipc.h>
#include  <sys/shm.h>
#include <netdb.h>

// Define cache parameters
#define CACHE_SIZE_MIN 4096 // 4KB
#define CACHE_SIZE_MAX 2097152 // 2MB
#define FILENAME_MAX_LENGTH 256
#define CACHE_ENTRY_MAX 100 // Maximum number of entries in the cache
#define MAX_FILE_SIZE_TO_CACHE 4500 // 1KB

#define BUFFER_SIZE 4096
#define MAX_CONNECTIONS 10
#define EOL "\r\n"
#define EOL_SIZE 2
#define MAX_PATH_LEN 500
#define DEF_BUF_SIZE 512
#define MAX_CACHE_ENTRIES 500

// Define cache entry structure
typedef struct {
    unsigned long file_id;
    char* content;
    long size;
    int is_used;
    unsigned int offset;
    int shm_id;
} CacheEntry;

// Define cache structure

typedef struct {
    CacheEntry entries[MAX_CACHE_ENTRIES];
    char* contents;
    int current_size;
    int size_limit;
    sem_t *mutex;
} Cache;

// Function prototypes
unsigned long generate_simple_hash(const char* str);
Cache* initialize_cache(size_t size_limit);
CacheEntry* fetch_file(Cache* cache, const char* filename, char* query, char* short_file_path);
void cleanup_cache(Cache* cache);

#endif /* CACHE_H */
