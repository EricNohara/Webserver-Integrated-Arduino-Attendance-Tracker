#ifndef CACHE_H
#define CACHE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <semaphore.h>


// Define cache parameters
#define CACHE_SIZE_MIN 4096 // 4KB
#define CACHE_SIZE_MAX 2097152 // 2MB
#define FILENAME_MAX_LENGTH 256
#define CACHE_ENTRY_MAX 100 // Maximum number of entries in the cache

// Define cache entry structure
typedef struct {
    char filename[FILENAME_MAX_LENGTH];
    char* content;
    size_t size;
} CacheEntry;

// Define cache structure
typedef struct {
    size_t size_limit;
    size_t current_size;
    CacheEntry* entries;
    int next_index;
    sem_t *mutex;  // Semaphore for synchronizing access to the cache
} Cache;

// Function prototypes
unsigned long generate_simple_hash(const char* str);
Cache* initialize_cache(size_t size_limit);
char* fetch_file(Cache* cache, char* filename);
void cleanup_cache(Cache* cache);

#endif /* CACHE_H */
