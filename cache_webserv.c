#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h> // Needed for sendfile on macOS
#include <unistd.h>
#include "cache.h" // Include the cache header file
#include <semaphore.h>

Cache* cache;
#define MAX_FILE_SIZE_TO_CACHE 4500 // 1KB
#define BUFFER_SIZE 4096
#define MAX_CONNECTIONS 10
#define EOL "\r\n"
#define EOL_SIZE 2
#define MAX_PATH_LEN 500
#define DEF_BUF_SIZE 512


typedef struct {
    char* ext;
    char* mediatype;
} extn;

int sockfd;
volatile sig_atomic_t sigint_received = 0;

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

// 501 response
void send_501(int fd)
{
    // Open the HTML file for reading
    FILE* html_file = fopen("errorhtml/501.html", "r");
    if (html_file == NULL) {
        perror("Failed to fulfill client request.\n");
        send_http_res(fd, "HTTP/1.1 Error 501\r\n");
        send_http_res(fd, "Server: Web Server in C\r\n\r\n");
        send_http_res(fd, "<html><head><title>501 Error</title></head><body><p>Error 501.</p></body></html>");
        return;
    }

    // Send HTTP response headers
    send_http_res(fd, "HTTP/1.1 Error 501\r\n");
    send_http_res(fd, "Server: Web Server in C\r\n\r\n");
    send_http_res(fd, "<html><head><title>501 Error</title></head><body><p>Error 501</p></body></html>");

    // Read and send the content of the HTML file
    char buffer[1024];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), html_file)) > 0) {
        if (send(fd, buffer, bytes_read, 0) == -1) {
            perror("Error sending HTML content");
            break;
        }
    }

    fclose(html_file); // Close the HTML file
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

// Resolve Requested Resource
char* resolve_req_resource(char* request, char* resource)
{
    if (!request || *request == '\0') {
        printf("Unknown Request!\n");
        return NULL;
    }

    if (request[strlen(request) - 1] == '/') {
        strcat(request, "/static/index.html"); // Default to index.html if the path ends with '/'
    }

    strcpy(resource, get_server_root_dir());
    strcat(resource, request);

    return resource;
}

// Open Requested File
int open_req_file(char* resource)
{
    int fd = open(resource, O_RDONLY);
    if (fd == -1) {
        perror("File open error");
    }
    return fd;
}

// 404: Not found response
void send_404(int fd)
{
    // Open the HTML file for reading
    FILE* html_file = fopen("errorhtml/404.html", "r");
    if (html_file == NULL) {
        perror("Failed to open HTML file");
        send_http_res(fd, "HTTP/1.1 Error 404\r\n");
        send_http_res(fd, "Server: Web Server in C\r\n\r\n");
        send_http_res(fd, "<html><head><title>404 Error</title></head><body><p>Error 404.</p></body></html>");
        return;
    }

    // Send HTTP response headers
    send_http_res(fd, "HTTP/1.1 Error 404\r\n");
    send_http_res(fd, "Server: Web Server in C\r\n\r\n");
    send_http_res(fd, "<html><head><title>404 Error</title></head><body><p>Error 404</p></body></html>");

    // Read and send the content of the HTML file
    char buffer[1024];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), html_file)) > 0) {
        if (send(fd, buffer, bytes_read, 0) == -1) {
            perror("Error sending HTML content");
            break;
        }
    }

    fclose(html_file); // Close the HTML file
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

// function to generate directory listing upon request
void generate_dir_listing(const char* directory, int client_fd)
{
    DIR* dir;
    struct dirent* entry;

    // try to open the directory for reading
    if ((dir = opendir(directory)) == NULL) {
        perror("Error: failed to open directory!\n");
        send_404(client_fd); // Send 404 if directory cannot be opened
        return;
    }

    // Send HTTP response headers
    send_http_res(client_fd, "HTTP/1.1 200 OK\r\n");
    send_http_res(client_fd, "Content-Type: text/plain\r\n\r\n");

    // read dir entries and send HTML list links
    while ((entry = readdir(dir)) != NULL) {
        send_http_res(client_fd, entry->d_name); // display name of entry
        send_http_res(client_fd, "\n"); // display name of entry
    }

    closedir(dir); // close the directory
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

int create_temp_file(const char* content) {
    char temp_filename[] = "/tmp/cache_temp_file_XXXXXX";
    int temp_fd = mkstemp(temp_filename); // Create a temporary file
    if (temp_fd == -1) {
        perror("mkstemp");
        return -1;
    }

    // Write the content to the temporary file
    if (write(temp_fd, content, strlen(content)) == -1) {
        perror("write");
        close(temp_fd);
        unlink(temp_filename); // Remove the temporary file if write fails
        return -1;
    }

    // Seek back to the beginning of the file for reading
    if (lseek(temp_fd, 0, SEEK_SET) == -1) {
        perror("lseek");
        close(temp_fd);
        unlink(temp_filename);
        return -1;
    }

    return temp_fd;
}

int handle_client_req(int client_fd, Cache* cache) {
    char request[DEF_BUF_SIZE];
    char resource[DEF_BUF_SIZE];
    char query[DEF_BUF_SIZE] = { 0 };
    char* requested_resource;

    // Parse HTTP Request
    if (!(requested_resource = parse_HTTP_req(client_fd, request))) {
        send_404(client_fd);
        return -1;
    }

    // Extract the query string from the request
    parse_query_string(request, query);

    printf("Client requested %s\n", request);

    // Resolve Requested Resource
    if (!resolve_req_resource(requested_resource, resource)) {
        send_404(client_fd);
        return -1;
    }

    // Check if the requested resource is a directory
    struct stat path_stat;
    if (stat(resource, &path_stat) == 0 && S_ISDIR(path_stat.st_mode)) {
        generate_dir_listing(resource, client_fd);
        return 0;
    }

    // Check if the requested file extension corresponds to a supported MIME type
    char* ext = strrchr(resource, '.');
    if (ext) ext++; // Move past the dot to the extension itself
    char* mime_type = is_supported_type(ext);
    if (ext && mime_type == NULL) {
        send_404(client_fd);
        return -1;
    }

    // Check if the requested file exists
    if (access(resource, F_OK) == -1) {
        send_404(client_fd);
        return -1;
    }

    // Check if the file can be cached and is smaller than the maximum size allowed for caching
    if (strcmp(ext, ".cgi") != 0 && path_stat.st_size <= MAX_FILE_SIZE_TO_CACHE) {
        sem_wait(cache->mutex);
        char* cached_content = fetch_file(cache, resource);
        if (cached_content != NULL) {
            // File found in cache, create temporary file with cached content
            int temp_fd = create_temp_file(cached_content);
            if (temp_fd == -1) {
                sem_post(cache->mutex);
                send_501(client_fd);
                return -1; // Error creating temp file
            }
            char content_type[50];
            sprintf(content_type, "Content-Type: %s\r\n\r\n", mime_type);
            send_http_res(client_fd, "HTTP/1.1 200 OK\r\n");
            send_http_res(client_fd, content_type);
            transfer_file(temp_fd, client_fd); // Send cached content from temp file
            close(temp_fd);
            sem_post(cache->mutex);
            return 0; // Success
        }
        sem_post(cache->mutex);
    }

    // If not cached and not a CGI script, handle file delivery
    if (strcmp(ext, ".cgi") == 0) {
        handle_cgi_script_req(resource, query, client_fd);
        return 0;
    } else {
        int file_fd = open_req_file(resource);
        if (file_fd == -1) {
            send_404(client_fd);
            return -1;
        }
        send_http_res(client_fd, "HTTP/1.1 200 OK\r\n");
        char content_type[50];
        sprintf(content_type, "Content-Type: %s\r\n\r\n", mime_type);
        send_http_res(client_fd, content_type);
        transfer_file(file_fd, client_fd); // Send file from disk
        close(file_fd);
    }

    close(client_fd);
    return 0;
}






int main(int argc, char* argv[])
{
    cache = initialize_cache(CACHE_SIZE_MAX);
    int newsockfd, port_num;
    int opt = 1; // must be non-zero value
    pid_t p;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    if (signal(SIGINT, sigint_handler) == SIG_ERR) // set up signal handler to close socket on exit
        error("Error setting up signal handler!\n");

    if (argc != 2) // check number of command line args
        error("Error: incorrent number of args for webserv!\n");

    port_num = atoi(argv[1]); // parse port from command line args

    if (port_num >= 65536 || port_num < 5000) // validate port number
        error("Error: invalid port number, must be in the range 5000-65536");

    // Create socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        error("Error: failed to open socket!\n");

    // clear address structure if needed
    bzero((char*)&server_addr, sizeof(server_addr));

    // set server socket options to be able to reuse address/port
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int)) < 0)
        error("Error: failed to set socket options with setsockopt!\n");

    // configure server_addr structure for bind
    server_addr.sin_family = AF_INET; // server byte order
    server_addr.sin_addr.s_addr = INADDR_ANY; // auto-fill with my IP
    server_addr.sin_port = htons(port_num); // convert port num to network byte order

    // bind socket to inputted port
    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
        error("Error: failed to bind server to port!\n");

    // listen on the socket with specified max connection requests queued
    if (listen(sockfd, MAX_CONNECTIONS) < 0)
        error("Error: cannot listen to client requests!\n");
    printf("Link: http://localhost:%i/\n", port_num);
    printf("Listening to client requests on port %i...\n", port_num);
    fflush(stdout);

    // accept incoming connections
    while (!sigint_received) {
        if ((newsockfd = accept(sockfd, (struct sockaddr*)&client_addr, &client_addr_len)) < 0)
            error("Error: failed to accept client request!\n");

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
            handle_client_req(newsockfd, cache); // Handle connection
            exit(EXIT_SUCCESS); // Terminate child process
        } else // Parent process
            close(newsockfd); // Parent doesn't need this socket
    }

    cleanup_cache(cache);
    close(sockfd);
    exit(EXIT_SUCCESS);
}