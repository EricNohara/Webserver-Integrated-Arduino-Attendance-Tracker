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

#define DEF_BUF_SIZE 10000
#define MAX_FILE_SIZE_TO_CACHE (1024 * 1024) // 1MB
#define MAX_CACHE_ENTRIES 1024

typedef struct {
    unsigned long file_id;
    char* content;
    long size;
} CacheEntry;

typedef struct {
    CacheEntry entries[MAX_CACHE_ENTRIES];
    int current_size;
    int size_limit;
    sem_t *mutex;
} Cache;

unsigned long generate_simple_hash(const char* str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }
    return hash;
}

char* normalize_path(const char* path) {
    if (!path) return NULL;
    char* normalized = malloc(strlen(path) + 1);
    if (!normalized) return NULL;

    const char *src = path;
    char *dst = normalized;
    while (*src) {
        *dst = *src++;
        if (*dst == '/') {
            while (*src == '/') src++;  // Skip consecutive slashes
        }
        dst++;
    }
    *dst = '\0';
    return normalized;
}

Cache* initialize_cache(int size_limit) {
    shm_unlink("/myCache");
    sem_unlink("/myCacheMutex");
    int shm_fd = shm_open("/myCache", O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        return NULL;
    }

    if (ftruncate(shm_fd, sizeof(Cache)) == -1) {
        perror("ftruncate");
        close(shm_fd);
        return NULL;
    }

    Cache* shared_cache = mmap(NULL, sizeof(Cache), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_cache == MAP_FAILED) {
        perror("mmap");
        close(shm_fd);
        return NULL;
    }

    shared_cache->mutex = sem_open("/myCacheMutex", O_CREAT, 0666, 1);
    if (shared_cache->mutex == SEM_FAILED) {
        perror("Semaphore initialization failed");
        munmap(shared_cache, sizeof(Cache));
        close(shm_fd);
        sem_unlink("/myCacheMutex");
        return NULL;
    }

    shared_cache->current_size = 0;
    shared_cache->size_limit = size_limit;

    close(shm_fd);
    return shared_cache;
}

void cleanup_cache(Cache* cache) {
    sem_close(cache->mutex);
    munmap(cache, sizeof(Cache));
    shm_unlink("/myCache");
    sem_unlink("/myCacheMutex");
}
char* fetch_file(Cache* cache, const char* filename) {
    char* normalized_filename = normalize_path(filename);
    if (!normalized_filename) {
        printf("Error: Unable to normalize filename '%s'.\n", filename);
        return NULL;
    }

    unsigned long file_id = generate_simple_hash(normalized_filename);

    // Check if the file is in the cache
    for (int i = 0; i < MAX_CACHE_ENTRIES; i++) {
        if (cache->entries[i].content != NULL && cache->entries[i].file_id == file_id) {
            printf("Cache hit: File '%s' found in cache.\n", normalized_filename);
            free(normalized_filename);
            return cache->entries[i].content;
        }
    }

    printf("Cache miss: File '%s' not found in cache. Loading from disk...\n", normalized_filename);
    struct stat st;
    memset(&st, 0, sizeof(struct stat)); // Initialize st structure
    if (stat(normalized_filename, &st) != 0) {
        printf("Error: File '%s' does not exist.\n", normalized_filename);
        free(normalized_filename);
        return NULL; // File does not exist
    }

    if (st.st_size > MAX_FILE_SIZE_TO_CACHE) {
        printf("Error: File '%s' is too large to cache (size: %lld).\n", normalized_filename, (long long)st.st_size);
        free(normalized_filename);
        return NULL; // File too large to cache
    }

    FILE *file = fopen(normalized_filename, "rb");
    if (!file) {
        printf("Error: Unable to open file '%s'.\n", normalized_filename);
        free(normalized_filename);
        return NULL;
    }

    char *buffer = (char*)malloc(st.st_size + 1);
    if (!buffer) {
        fclose(file);
        printf("Error: Failed to allocate memory for file '%s'.\n", normalized_filename);
        free(normalized_filename);
        return NULL;
    }

    if (fread(buffer, st.st_size, 1, file) != 1) {
        free(buffer);
        fclose(file);
        printf("Error: Failed to read file '%s'.\n", normalized_filename);
        free(normalized_filename);
        return NULL;
    }
    printf("The file size is: %lli\n", st.st_size);
    buffer[st.st_size] = '\0'; // Ensure null-terminated
    fclose(file);

    // Add file to cache if cache is not full
    sem_wait(cache->mutex);
    if (cache->current_size + st.st_size <= cache->size_limit) {
    printf("Adding file '%s' to cache. Current cache size: %d\n", normalized_filename, cache->current_size);
    int index = cache->current_size;
    printf("Index before assignment: %d\n", index);

    cache->entries[index].file_id = file_id;
    printf("File ID assigned: %lu\n", cache->entries[index].file_id);

    cache->entries[index].content = buffer;
    printf("Content assigned: %p\n", (void *)cache->entries[index].content);

    cache->entries[index].size = st.st_size;
    printf("Size assigned: %lld\n", (long long)cache->entries[index].size);

    cache->current_size += st.st_size;
    printf("Updated cache size: %d\n", cache->current_size);
} else {
        printf("Cache full: Cannot add file '%s' to cache.\n", normalized_filename);
        free(buffer); // Free buffer if not caching
    }
    sem_post(cache->mutex);

    free(normalized_filename);
    return buffer;
}
