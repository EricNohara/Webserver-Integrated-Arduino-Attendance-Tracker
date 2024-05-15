#define _XOPEN_SOURCE 500 // required for sigaltstack and stack_t

#include "my_threads.h"
#include <arpa/inet.h>
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <netinet/in.h>
#include <pwd.h>
#include <setjmp.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h> // Needed for sendfile on macOS
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include "howell_cache.h"

#define BUFFER_SIZE 4096
#define EOL "\r\n"
#define EOL_SIZE 2
#define MAX_PATH_LEN 500
#define DEF_BUF_SIZE 512

typedef struct {
    char* ext;
    char* mediatype;
} extn;

int sockfd, newsockfd;
volatile sig_atomic_t sigint_received = 0;
int is_cached;
Cache* global_cache;

// Media types
extn extensions[] = {
    { "gif", "image/gif" },
    { "gz", "image/gz" },
    { "htm", "text/html" },
    { "html", "text/html" },
    { "ico", "image/ico" },
    { "jpeg", "image/jpeg" },
    { "jpg", "image/jpg" },
    { "pdf", "application/pdf" },
    { "php", "text/html" },
    { "png", "image/png" },
    { "rar", "application/octet-stream" },
    { "tar", "image/tar" },
    { "txt", "text/plain" },
    { "zip", "application/octet-stream" }, // Note: Duplicate MIME type entries for 'zip'
    { "cgi", "application/x-httpd-cgi" },
    { 0, 0 } // End of array marker
};

void sigint_handler(int signum)
{
    if (signum == SIGINT) {
        printf("Exiting process and closing socket file descriptor %i.\n", sockfd);
        close(sockfd);
        close(newsockfd); // Parent doesn't need this socket
        exit(EXIT_SUCCESS);
    }
}

// error wrapper which prints error and exits
void error(const char* msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

// get file size using lseek and returns file size in bytes, or -1 on error
int get_file_size(int fd)
{
    off_t size = lseek(fd, 0, SEEK_END); // Seek to the end of the file
    if (size == -1) { // Handle the error if lseek fails
        perror("Error: failed to seek file with lseek!\n");
        return -1;
    }

    // Optionally reset the file position to the beginning of the file
    if (lseek(fd, 0, SEEK_SET) == -1) { // Handle error on repositioning
        perror("Error: failed to reset file pointer!\n");
        return -1;
    }

    return (int)size; // Return file size
}

// send HTTP response over socked fd, print error on fail
void send_http_res(int fd, char* msg)
{
    if (send(fd, msg, strlen(msg), 0) == -1) // Attempt to send the message
        perror("Error: failed to send HTTP response!\n");
}

// recieve data from socket in 1 byte increments until EOL sequence
int recieve_until_EOL(int fd, char* buffer)
{
    char* p = buffer; // used to traverse the buffer
    int eol_matched = 0; // tracks number of consecutive EOL chars matched

    while (recv(fd, p, 1, 0) > 0) { // recieve 1 byte at a time
        if (*p == EOL[eol_matched]) { // check if current byte is EOL char
            eol_matched++;
            if (eol_matched == EOL_SIZE) { // check if full EOL sequence matched
                *(p + 1 - EOL_SIZE) = '\0'; // terminate string before EOL sequence
                return strlen(buffer); // return length of received string
            }
        } else
            eol_matched = 0; // reset EOL match counter if no match
        p++; // move to next byte in buffer
    }

    return 0; // return 0 if the connection is closed before EOL encountered
}

// returns server root directory formatted as a string
char* get_server_root_dir()
{
    char* basePath = getenv("WEBROOT_PATH");
    if (!basePath) { // ensure that WEBROOT_PATH is set
        fprintf(stderr, "The environment variable WEBROOT_PATH is not set.\n");
        exit(EXIT_FAILURE);
    }

    static char fullPath[1024];
    snprintf(fullPath, sizeof(fullPath), "%s/", basePath);
    return strdup(fullPath);
}

void handle_cgi_script_req(char* script_path, char* query_str, int client_fd)
{
    // send initial HTTP 200 OK header to the client
    send_http_res(client_fd, "HTTP/1.1 200 OK\nServer: Web Server in C\nConnection: close\n");

    // redirect STDOUT to the client file descriptor to capture output from php-cgi
    if (dup2(client_fd, STDOUT_FILENO) == -1)
        error("Error: failed to redirect STDOUT!\n");

    // prepare environment for the CGI script
    char script_env[DEF_BUF_SIZE];
    char query_env[DEF_BUF_SIZE];
    sprintf(script_env, "SCRIPT_FILENAME=%s", script_path);
    sprintf(query_env, "QUERY_STRING=%s", query_str);

    char* env_vars[] = {
        "GATEWAY_INTERFACE=CGI/1.1",
        script_env,
        query_env,
        "REQUEST_METHOD=GET",
        "REDIRECT_STATUS=true",
        "SERVER_PROTOCOL=HTTP/1.1",
        "REMOTE_HOST=127.0.0.1",
        NULL
    };

    for (int i = 0; env_vars[i] != NULL; i++) {
        if (putenv(env_vars[i]) != 0)
            error("Error: failed to set environment variables!\n");
    }

    // Execute the CGI script
    if (execl(script_path, script_path, NULL) == -1)
        error("Error: failed to execute CGI script!\n");

    exit(EXIT_SUCCESS);
}

void handle_cgi_script_req_threaded(char* script_path, char* query_str, int client_fd)
{
    // send initial HTTP 200 OK header to the client
    send_http_res(client_fd, "HTTP/1.1 200 OK\nServer: Web Server in C\nConnection: close\n");

    // prepare environment for the CGI script
    char script_env[DEF_BUF_SIZE];
    char query_env[DEF_BUF_SIZE];
    sprintf(script_env, "SCRIPT_FILENAME=%s", script_path);
    sprintf(query_env, "QUERY_STRING=%s", query_str);

    char* env_vars[] = {
        "GATEWAY_INTERFACE=CGI/1.1",
        script_env,
        query_env,
        "REQUEST_METHOD=GET",
        "REDIRECT_STATUS=true",
        "SERVER_PROTOCOL=HTTP/1.1",
        "REMOTE_HOST=127.0.0.1",
        NULL
    };

    for (int i = 0; env_vars[i] != NULL; i++) {
        if (putenv(env_vars[i]) != 0) {
            perror("Error: failed to set environment variables!\n");
            return;
        }
    }

    // Execute the CGI script
    int p = fork();

    if (p < 0) { // if failed to fork, close sockets and exit
        close(client_fd);
        error("Error: cannot fork to handle client request!\n");
    }

    if (p == 0) { // Handle child process
        // redirect STDOUT to the client file descriptor to capture output
        if (dup2(client_fd, STDOUT_FILENO) == -1)
            error("Error: failed to redirect STDOUT!\n");

        // Execute the CGI script
        if (execl(script_path, script_path, NULL) == -1)
            error("Error: failed to execute CGI script!\n");

        exit(EXIT_SUCCESS); // Terminate child process
    }

    close(client_fd);
}

// arranges resource locations, checks media type, serves files in web root, and sends HTTP error codes
int transfer_file(int src_fd, int dest_fd)
{
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read, bytes_written, total_bytes_written = 0;
    while ((bytes_read = read(src_fd, buffer, BUFFER_SIZE)) > 0) {
        bytes_written = write(dest_fd, buffer, bytes_read);
        if (bytes_written <= 0) {
            perror("Error: failed write\n");
            return -1; // Exit on write error
        }
        total_bytes_written += bytes_written;
    }
    if (bytes_read == -1) {
        perror("Error: failed read\n");
        return -1; // Exit on read error
    }
    return total_bytes_written; // Return total bytes written
}

// parse the HTTP request
char* parse_HTTP_req(int fd, char* request)
{
    if (recieve_until_EOL(fd, request) == 0) {
        printf("Receive Failed\n");
        return NULL;
    }

    char* ptr = strstr(request, " HTTP/");
    if (!ptr) {
        printf("NOT HTTP!\n");
        return NULL;
    }
    *ptr = '\0'; // Null terminate at the end of the URL part

    return request + 4;
}

// function to resolve requested resource
char* resolve_req_resource(char* request, char* resource)
{
    if (!request || *request == '\0') {
        printf("Unknown Request!\n");
        return NULL;
    }

    if (request[strlen(request) - 1] == '/') // Default to listing current directory if / request
        strcat(request, ".");

    strcpy(resource, get_server_root_dir());
    strcat(resource, request);

    return resource;
}

// Open Requested File
int open_req_file(char* resource)
{
    int fd = open(resource, O_RDONLY);
    if (fd == -1)
        perror("File open error");

    return fd;
}

// 404: Not found response
void send_404(int fd)
{
    send_http_res(fd, "HTTP/1.1 404 Not Found\r\n");
    send_http_res(fd, "Server: Web Server in C\r\n\r\n");
    send_http_res(fd, "<html><head><title>404 Not Found</title></head><body><h2>Error 404: Not Found</h2></body></html>");
}

// 501 response
void send_501(int fd)
{
    send_http_res(fd, "HTTP/1.1 501 Not Implemented\r\n");
    send_http_res(fd, "Server: Web Server in C\r\n\r\n");
    send_http_res(fd, "<html><head><title>501 Not Implemented</title></head><body><h1>Error 501: Not Implemented</h1></body></html>");
}

// get and return MIME type for requested file
char* is_supported_type(const char* ext)
{
    for (int i = 0; extensions[i].ext; ++i) {
        if (strcmp(ext, extensions[i].ext) == 0)
            return extensions[i].mediatype;
    }
    return NULL;
}

void generate_dir_listing(const char* directory, int client_fd)
{
    if (dup2(client_fd, STDOUT_FILENO) == -1)
        error("Error: failed to redirect STDOUT!\n");

    send_http_res(client_fd, "HTTP/1.1 200 OK\r\n");
    send_http_res(client_fd, "Content-Type: text/plain\r\n\r\n");

    execlp("ls", "ls", "-a", "-l", directory, NULL);
    exit(EXIT_SUCCESS);
}

// function to format the mode as a null terminated string
void mode_to_str(mode_t mode, char str[])
{
    str[0] = (S_ISDIR(mode)) ? 'd' : '-';
    str[1] = (mode & S_IRUSR) ? 'r' : '-';
    str[2] = (mode & S_IWUSR) ? 'w' : '-';
    str[3] = (mode & S_IXUSR) ? 'x' : '-';
    str[4] = (mode & S_IRGRP) ? 'r' : '-';
    str[5] = (mode & S_IWGRP) ? 'w' : '-';
    str[6] = (mode & S_IXGRP) ? 'x' : '-';
    str[7] = (mode & S_IROTH) ? 'r' : '-';
    str[8] = (mode & S_IWOTH) ? 'w' : '-';
    str[9] = (mode & S_IXOTH) ? 'x' : '-';
    str[10] = '\0'; // Null-terminate the string
}

// function to generate directory listing upon request
void generate_dir_listing_threaded(const char* directory, int client_fd)
{
    DIR* dir;
    struct dirent* entry;
    struct stat file_stat;

    // try to open the directory for reading
    if ((dir = opendir(directory)) == NULL) {
        perror("Error: failed to open directory!\n");
        send_404(client_fd); // Send 404 if directory cannot be opened
        return;
    }

    // Send HTTP response headers
    send_http_res(client_fd, "HTTP/1.1 200 OK\r\n");
    send_http_res(client_fd, "Content-Type: text/plain\r\n\r\n");

    // read dir entries and send formatted directory listing
    while ((entry = readdir(dir)) != NULL) {
        char filepath[MAX_PATH_LEN];
        snprintf(filepath, sizeof(filepath), "%s/%s", directory, entry->d_name);

        if (stat(filepath, &file_stat) == -1) { // stat the current file
            fprintf(stderr, "Error: failed to get stats for %s!\n", filepath);
            continue; // Skip to the next entry
        }

        char mode[11]; // size of mode string
        mode_to_str(file_stat.st_mode, mode);

        char* time = ctime(&file_stat.st_mtime); // get formatted time from file stats
        if (time[strlen(time) - 1] == '\n') // remove newline char if needed
            time[strlen(time) - 1] = '\0';

        // // Format and send directory entry in ls -l format
        char dir_entry[DEF_BUF_SIZE]; // Adjust the buffer size as needed
        snprintf(dir_entry, sizeof(dir_entry), "%s %ld %s %s %ld %s %s\n",
            mode,
            file_stat.st_nlink,
            getpwuid(file_stat.st_uid)->pw_name,
            getgrgid(file_stat.st_gid)->gr_name,
            file_stat.st_size,
            time,
            entry->d_name);

        send_http_res(client_fd, dir_entry);
    }

    closedir(dir); // close the directory
    system("ls -l -a");
}

// function which parses the query string and removes it from the initial request
void parse_query_string(const char* request, char* query_string)
{
    char* query_start = strchr(request, '?'); // find ? char indicating start of query string

    // handle case where query string is present
    if (query_start != NULL) {
        strcpy(query_string, query_start + 1); // Copy the query string into query_string
        *query_start = '\0'; // Remove the query string from the original request
    } else
        strcpy(query_string, "");
}

// The child thread will execute this function
void handle_client_req_threaded(void* arg)
{
    assert(arg == NULL);
    // thread functionality here...
    int client_fd = newsockfd;

    char request[DEF_BUF_SIZE];
    char resource[DEF_BUF_SIZE];
    char query[DEF_BUF_SIZE] = { 0 };
    char* requested_resource;

    // Parse HTTP Request
    if (!(requested_resource = parse_HTTP_req(client_fd, request))) {
        send_404(client_fd);
        goto jump;
    }

    // extract the query string from the request
    parse_query_string(request, query);

    printf("Client requested %s\n", request);

    // Resolve Requested Resource
    if (!resolve_req_resource(requested_resource, resource))
        goto jump;

    // Check if the requested resource is a directory
    struct stat path_stat;
    if (stat(resource, &path_stat) == 0) {
        if (S_ISDIR(path_stat.st_mode)) {
            generate_dir_listing_threaded(resource, client_fd); // Generate and send directory listing
            goto jump;
        }
    } else {
        send_404(client_fd);
        goto jump;
    }

    // Check if the requested file extension corresponds to a supported MIME type
    char* ext = strrchr(resource, '.');
    char* mime_type = is_supported_type(ext + 1);
    if (ext && mime_type == NULL) { // Send 404 response due to type not being supported
        send_404(client_fd);
        goto jump;
    }

    // Check if the requested file exists
    if (access(resource, F_OK) == -1) {
        send_404(client_fd); // Send 404 if file doesn't exist
        goto jump;
    }

    if (strcmp(ext, ".cgi") == 0) {
        handle_cgi_script_req_threaded(resource, query, client_fd);
        goto jump;
    }

    // Open Requested File
    int file_fd = open_req_file(resource);
    if (file_fd == -1) { // Send 404 Not Found response
        send_404(client_fd);
        goto jump;
    }

    char content_type[50];
    sprintf(content_type, "Content-Type: %s\r\n\r\n", mime_type);

    // Send 200 OK response
    send_http_res(client_fd, "HTTP/1.1 200 OK\r\n");
    send_http_res(client_fd, content_type);

    // Transfer File Content
    transfer_file(file_fd, client_fd);

jump:
    // Close file descriptors
    close(file_fd);
    close(client_fd);
}


int check_cache(Cache* cache, int client_fd, char* content_type, char* resource, char* query, char* short_file_path){
    //Check Cache
    sem_wait(cache->mutex);

    CacheEntry* entry = fetch_file(cache, resource, query, short_file_path);
    if(entry != NULL){
        send_http_res(client_fd, "HTTP/1.1 200 OK\r\n");
        send_http_res(client_fd, content_type);

        char buffer[BUFFER_SIZE];
        ssize_t bytes_read, bytes_written, total_bytes_written = 0;
        int count = 0;
        while (count*BUFFER_SIZE < entry->size) {

            if(BUFFER_SIZE < entry->size - count*BUFFER_SIZE){
                bytes_read = BUFFER_SIZE;
            }
            else{
                bytes_read = entry->size - count*BUFFER_SIZE;
            }

            memcpy(buffer, entry->content + count*BUFFER_SIZE, BUFFER_SIZE);

            bytes_written = write(client_fd, buffer, bytes_read);
            if (bytes_written <= 0) {
                perror("Error: failed write\n");
                return -1; // Exit on write error
            }
            total_bytes_written += bytes_written;
            count++;
        }
        close(client_fd);
        sem_post(cache->mutex);
        return 0;
    }
    sem_post(cache->mutex);
    return -1;
}

// Function to handle client requests
int handle_client_req(int client_fd)
{
    char request[DEF_BUF_SIZE];
    char resource[DEF_BUF_SIZE];
    char query[DEF_BUF_SIZE] = { 0 };
    char* requested_resource;
    // Parse HTTP Request
    if (!(requested_resource = parse_HTTP_req(client_fd, request))) {
        send_404(client_fd);
        return -1;
    }

    // extract the query string from the request
    parse_query_string(request, query);

    printf("Client requested %s\n", request);

    // Resolve Requested Resource
    if (!resolve_req_resource(requested_resource, resource))
        return -1;

    // Check if the requested resource is a directory
    struct stat path_stat;
    if (stat(resource, &path_stat) == 0) {
        if (S_ISDIR(path_stat.st_mode)) {
            generate_dir_listing(resource, client_fd); // generate and send directory listing to client
            return 0;
        }
    } 
    else if (query[0] == '\0'){
        send_404(client_fd);
        return -1;
    }
    

    // Check if the requested file extension corresponds to a supported MIME type
    char* ext = strrchr(resource, '.');
    char* mime_type = is_supported_type(ext + 1);
    if (ext && mime_type == NULL) { // Send 404 response due to type not being supported
        send_404(client_fd);
        return -1;
    }

    if (strcmp(ext, ".cgi") == 0) {
        handle_cgi_script_req(resource, query, client_fd);
        return 0;
    }

    char content_type[50];
    sprintf(content_type, "Content-Type: %s\r\n\r\n", mime_type);


    if(is_cached == 1){
        int valid = check_cache(global_cache, client_fd, content_type, resource, query, requested_resource);
        if(valid == 0){
            return 0;
        }
    }

    // Open Requested File
    int file_fd = open_req_file(resource);
    if (file_fd == -1) { // Send 404 Not Found response
        send_404(client_fd);
        return -1;
    }

    // Send 200 OK response
    send_http_res(client_fd, "HTTP/1.1 200 OK\r\n");
    send_http_res(client_fd, content_type);

    // Transfer File Content
    transfer_file(file_fd, client_fd);

    // Close file descriptors
    close(file_fd);
    close(client_fd);

    return 0;
}

int main(int argc, char* argv[])
{
    int port_num;
    int opt = 1; // must be non-zero value
    pid_t p;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    if (signal(SIGINT, sigint_handler) == SIG_ERR) // set up signal handler to close socket on exit
        error("Error setting up signal handler!\n");

    if (argc < 2) // check number of command line args
        error("Error: incorrent number of args for webserv!\n");

    int c;
    char* port_str = NULL;
    char* cache_size_str = NULL;
    int is_threaded = 0;

    while((c = getopt(argc, argv, "p:c:t")) != -1)
    {
        switch (c)
        {
        case 'p':
            port_str = optarg;
            break;
        case 'c':
            cache_size_str = optarg;
            is_cached = 1;
            break;
        case 't':
            is_threaded = 1;
            break;
            
        case '?':
            if (optopt == 'c' || optopt == 'p')
                printf("Option -%c requires an argument.\n", optopt);
            else if (isprint (optopt))
                printf("Unknown option `-%c'.\n", optopt);
            else
                printf ("Unknown option character `\\x%x'.\n", optopt);
            return 1;
        default:
            abort ();
        }
    }

    port_num = atoi(port_str); // parse port from command line args

    if (port_num >= 65536 || port_num < 5000) // validate port number
        error("Error: invalid port number, must be in the range 5000-65536");


    if(cache_size_str != NULL){
        int cache_size = atoi(cache_size_str);
        if (cache_size > CACHE_SIZE_MAX || port_num < CACHE_SIZE_MIN) // validate port number
            error("Error: invalid cache size number, must be in the range 4096-2097152");

        global_cache = initialize_cache(cache_size);
    }

    // Create socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        error("Error: failed to open socket!\n");

    // set server socket options to be able to reuse address/port
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int)) < 0)
        error("Error: failed to set socket options with setsockopt!\n");

    // configure server_addr structure for bind
    server_addr.sin_family = AF_INET; // server byte order
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port_num); // convert port num to network byte order

    // bind socket to inputted port
    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
        error("Error: failed to bind server to port!\n");

    // listen on the socket with specified max connection requests queued
    if (listen(sockfd, SOMAXCONN) < 0)
        error("Error: cannot listen to client requests!\n");

    printf("Link: http://localhost:%i/\n", port_num);
    printf("Listening to client requests on port %i...\n", port_num);
    fflush(stdout);

    // accept incoming connections
    while (!sigint_received) {
        if ((newsockfd = accept(sockfd, (struct sockaddr*)&client_addr, &client_addr_len)) < 0)
            error("Error: failed to accept client request!\n");

        if (!is_threaded) {
            // fork process to handle client request
            p = fork();

            if (p < 0) { // if failed to fork, close sockets and exit
                close(sockfd);
                close(newsockfd);
                error("Error: cannot fork to handle client request!\n");
            }

            if (p == 0) { // This is the client process
                close(sockfd); // Close the original socket in child
                signal(SIGINT, SIG_IGN); // ignore SIGINT signals in children to avoid multiple signal handling
                handle_client_req(newsockfd); // Handle connection
                exit(EXIT_SUCCESS); // Terminate child process
            } else // Parent process
                close(newsockfd); // Parent doesn't need this socket
        } else
            create_thread(handle_client_req_threaded);
    }

    close(sockfd);
    exit(EXIT_SUCCESS);
}
