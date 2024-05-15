#include "howell_cache.h"

void exit_and_clean_shm(int shm_id)
{
    shmctl(shm_id, IPC_RMID, NULL);
}

Cache* initialize_cache(size_t size_limit){
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
    return shared_cache;
}

unsigned long generate_simple_hash(const char* str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }
    return hash;
}

CacheEntry* fetch_file(Cache* cache, const char* filename, char* query, char* short_file_path){
    unsigned long int file_hash = generate_simple_hash(filename);
    file_hash += generate_simple_hash(query);

    int cache_index = -1;
    for(int i = 0; i < MAX_CACHE_ENTRIES; i++){
        if(cache->entries[i].is_used && cache->entries[i].file_id == file_hash){
            cache_index = i;
            break;
        }
    }

    if(cache_index != -1){
        printf("Cache Hit!\n");
        cache->entries[cache_index].content = (char*)shmat(cache->entries[cache_index].shm_id, NULL, SHM_R | SHM_W); 
        return &cache->entries[cache_index];
    }
    //not in cache
    else{
        //check if file is too large for cache 
        if(query[0] == '\0'){
            struct stat st;
            memset(&st, 0, sizeof(struct stat)); // Initialize st structure
            if (stat(filename, &st) != 0) {
                printf("Error: File '%s' does not exist.\n", filename);
                return NULL; // File does not exist
            }

            if (st.st_size > cache->size_limit) {
                printf("Error: File '%s' is too large to cache (size: %lld).\n", filename, (long long)st.st_size);
                return NULL; // File too large to cache
            }
            //adding file to cache
            int fd = open(filename, O_RDONLY);
            if (fd == -1){
                perror("File open error");
                return NULL;
            }
            

            CacheEntry temp;
            temp.size = st.st_size;
            temp.file_id = file_hash;
            temp.is_used = 1;
            temp.shm_id = shmget(IPC_PRIVATE, st.st_size, IPC_CREAT | IPC_EXCL | 0666);
            if (temp.shm_id < 0) { // if failed, handle error
                perror("Error: cannot create shared memory segment for shared buffer!\n");
                exit(1);
            }

            temp.content = (char*)shmat(temp.shm_id, NULL, SHM_R | SHM_W); 
            if (temp.content == (void*)-1) { // handle error if attach memory fails
                perror("Error: cannot attach shared buffer memory segment");
                exit_and_clean_shm(temp.shm_id);
            }

            if(read(fd, temp.content, st.st_size) < 0){
                perror("Error: failed read\n");
                close(fd);
                return NULL;
            }
            
            int i = 0;
            while(st.st_size + cache->current_size > cache->size_limit)
            {
                if(cache->entries[i].is_used){
                    cache->entries[i].is_used = 0;
                    exit_and_clean_shm(cache->entries[i].shm_id);
                    cache->current_size -= cache->entries[i].size;
                }
                i++;
            }

            int cache_write_index;
            for(cache_write_index = 0; cache_write_index < MAX_CACHE_ENTRIES; cache_write_index++)
            {
                if(!cache->entries[cache_write_index].is_used){
                    break;
                }
            }

            cache->entries[cache_write_index] = temp;
            cache->current_size += temp.size;
            //cache->content_test = "testy";
            //memcpy(cache->contents, temp.content, temp.size);

            close(fd);
            return &cache->entries[cache_write_index];
        }
        else{
            char* token = strtok(query, "=");
            token = strtok(NULL, "=");

            if(token == NULL){
                return NULL;
            }

            char* host = strtok(token, ":");
            if(host == NULL){
                return NULL;
            }

            char* port_str = strtok(NULL, ":");

            int port;
            if(port_str == NULL){
                port = 80;
            }
            else{
                port = atoi(port_str);
            }

            struct hostent *server;
            struct sockaddr_in serv_addr;
            int sockfd, bytes, sent, received, total;
            char message[1024],response[cache->size_limit];

            
            sockfd = socket(AF_INET, SOCK_STREAM, 0);
            if (sockfd < 0) {
                printf("ERROR opening socket\n");
                return NULL;
            }

            server = gethostbyname(host);

            memset(&serv_addr,0,sizeof(serv_addr));
            serv_addr.sin_family = AF_INET;
            serv_addr.sin_port = htons(port);
            memcpy(&serv_addr.sin_addr.s_addr,server->h_addr,server->h_length);

            char *message_fmt = "GET %s HTTP/1.1\r\nHost: %s\r\n\r\n";

            sprintf(message,message_fmt, short_file_path, token);

            if (connect(sockfd,(struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0)
            {
                printf("ERROR connecting\n");
                return NULL;
            }
            
                /* send the request */
            total = strlen(message);
            sent = 0;
            printf("Sending request to remote server\n");
            do {
                bytes = write(sockfd,message+sent,total-sent);
                if (bytes < 0){
                    printf("ERROR writing message to socket\n");
                    return NULL;
                }
                    
                if (bytes == 0)
                    break;
                sent+=bytes;
            } while (sent < 0);

            //receive the response
            printf("Receiving response from remote server\n");
            memset(response,0,sizeof(response));
            total = sizeof(response)-1;
            received = 0;
            do {
                bytes = read(sockfd,response-received,total-received);
                if (bytes < 0){
                    printf("ERROR reading response from socket\n");
                    return NULL;
                }
                if (bytes == 0)
                    break;
                received+=bytes;
            } while(received < 0); //while (received < total);
            printf("Response received from remote server\n");

            if (received == total){
                printf("ERROR couldn't get all of the file\n");
                return NULL;
            }
                

            /* close the socket */
            close(sockfd);

            /* process response */
            char* response_strtok = malloc(sizeof(response));
            memcpy(response_strtok, response, sizeof(response));
            char* body = NULL;
            for(long unsigned int i = 0; i < strlen(response) - 4; i++){
                if(response[i] == '\r' && response[i + 1] == '\n' && response[i + 2] == '\r' && response[i + 3] == '\n'){
                    body = response + i + 4;
                    break;
                }
            }
            // char* body = strtok(response, "\n\n");
            // body = strtok(NULL, "\n\n");
            // // body = strtok(NULL, "\r\n\r\n");

            if(body == NULL){
                printf("Error: No content\n");
                return NULL;
            }
            // 


            int file_size = strlen(body);
            if (file_size > cache->size_limit) {
                printf("Error: File '%s' is too large to cache (size: %lld).\n", filename, (long long)file_size);
                return NULL; // File too large to cache
            }

            CacheEntry temp;
            temp.size = file_size;
            temp.file_id = file_hash;
            temp.is_used = 1;
            temp.shm_id = shmget(IPC_PRIVATE, file_size, IPC_CREAT | IPC_EXCL | 0666);
            if (temp.shm_id < 0) { // if failed, handle error
                perror("Error: cannot create shared memory segment for shared buffer!\n");
                exit(1);
            }

            temp.content = (char*)shmat(temp.shm_id, NULL, SHM_R | SHM_W); 
            if (temp.content == (void*)-1) { // handle error if attach memory fails
                perror("Error: cannot attach shared buffer memory segment");
                exit_and_clean_shm(temp.shm_id);
            }

            memcpy(temp.content, body, file_size);

            
            int i = 0;
            while(file_size + cache->current_size > cache->size_limit)
            {
                if(cache->entries[i].is_used){
                    cache->entries[i].is_used = 0;
                    exit_and_clean_shm(cache->entries[i].shm_id);
                    cache->current_size -= cache->entries[i].size;
                }
                i++;
            }

            int cache_write_index;
            for(cache_write_index = 0; cache_write_index < MAX_CACHE_ENTRIES; cache_write_index++)
            {
                if(!cache->entries[cache_write_index].is_used){
                    break;
                }
            }

            cache->entries[cache_write_index] = temp;
            cache->current_size += temp.size;
            return &cache->entries[cache_write_index];
        }    
    }

}

void cleanup_cache(Cache* cache) {

    for(int i = 0; i < MAX_CACHE_ENTRIES; i++){
        if(cache->entries[i].is_used){
            exit_and_clean_shm(cache->entries[i].shm_id);
        }
    }

    sem_close(cache->mutex);
    munmap(cache, sizeof(Cache));
    shm_unlink("/myCache");
    sem_unlink("/myCacheMutex");
}
