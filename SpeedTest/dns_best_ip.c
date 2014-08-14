#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>

#include "dns_best_ip.h"

#define PORT 80
#define MAX_RETRIES 3

#define TRUE 1
#define FALSE 0

#define DEBUG_LOG
#ifdef DEBUG_LOG
#define DLog(format, args...) printf(format , ##args);
#else
#define DLog(...)
#endif

int read_line(int fd, char *buffer, int size) {
    char next = '\0';
    char err;
    int i = 0;
    while ((i < (size - 1)) && (next != '\n')) {
        err = read(fd, &next, 1);

        if (err <= 0) break;

        if (next == '\r') {
            err = recv(fd, &next, 1, MSG_PEEK);
            if (err > 0 && next == '\n') {
                read(fd, &next, 1);
            } else {
                next = '\n';
            }
        }
        buffer[i] = next;
        i++;
    }
    buffer[i] = '\0';
    return i;
}

/**
 *  receive bytes from socket, but not store in buffer, just read data of the target_size
 */
int read_socket(int fd, char *buffer, int buffer_size, int target_size) {
    int bytes_recvd = 0;
    int retries = 0;
    int total_recvd = 0;
    
    do {
        buffer[0] = '\0';
        bytes_recvd = read(fd, buffer, buffer_size);
//        DLog("%s\n", buffer);
        if (bytes_recvd > 0) {
            target_size -= bytes_recvd;
            total_recvd += bytes_recvd;
        } else {
            retries++;
        }
    } while (retries < MAX_RETRIES && target_size > 0);

    if (bytes_recvd >= 0) {
        // Last read was not an error, return how many bytes were recvd
        return total_recvd;
    }
    
    // Last read was an error, return error code
    return -1;
}

int write_socket(int fd, char *msg, int size) {
    int bytes_sent = 0;
    int retries = 0;
    int total_sent = 0;

    while (retries < MAX_RETRIES && size > 0) {
        bytes_sent = write(fd, msg, size);

        if (bytes_sent > 0) {
            msg += bytes_sent;
            size -= bytes_sent;
            total_sent += bytes_sent;
        } else {
            retries++;
        }
    }

    if (bytes_sent >= 0) {
        // Last write was not an error, return how many bytes were sent
        return total_sent;
    }
    // Last write was an error, return error code
    return -1;
}

struct HeaderReturnStuct {
    int header_err_flag;
    int content_length;
};

struct HeaderReturnStuct read_headers(int sockfd) {
    struct HeaderReturnStuct header_return;
    header_return.header_err_flag = FALSE;
    header_return.content_length = -1;
    
    // DLog("\n--READ HEADERS--\n\n");
    
    while(1) {
        char header[8096];
        int len;
        char next;
        int err;
        char *header_value_start;
        len = read_line(sockfd, header, sizeof(header));

        if (len <= 0) {
            // Error in reading from socket
            header_return.header_err_flag = TRUE;
            continue;
        }

        // DLog("%s", header);

        if (strcmp(header, "\n") == 0) {
            // Empty line signals end of HTTP Headers
            return header_return;
        }

        // If the next line begins with a space or tab, it is a continuation of the previous line.
        err = recv(sockfd, &next, 1, MSG_PEEK);
        while (isspace(next) && next != '\n' && next != '\r') {
            if (err) {
                DLog("header space/tab continuation check err\n");
                // Not sure what to do in this scenario
            }
            // Read the space/tab and get rid of it 
            read(sockfd, &next, 1);
            
            // Concatenate the next line to the current running header line
            len = len + read_line(sockfd, header + len, sizeof(header) - len);
            err = recv(sockfd, &next, 1, MSG_PEEK);
        }

        // Find first occurence of colon, to split by header type and value
        header_value_start = strchr(header, ':');
        if (header_value_start == NULL) {
            // Invalid header, not sure what to do in this scenario
            DLog("invalid header\n");
            header_return.header_err_flag = TRUE;
            continue;
        }
        int header_type_len = header_value_start - header;

        // Increment header value start past colon
        header_value_start++;
        // Increment header value start to first non-space character
        while (isspace(*header_value_start) && (*header_value_start != '\n') && (*header_value_start != '\r')) {
            header_value_start++;
        }
        int header_value_len = len - (header_value_start - header);

        // We only care about Content-Length
        if (strncasecmp(header, "Content-Length", header_type_len) == 0) {
            header_return.content_length = atoi(header_value_start);
        } else if (strncasecmp(header, "Location", header_type_len) == 0) {
//            strncpy(location, header_value_start, header_value_len);
        }
    }
}

char* split_path(const char *path, char **file_out) {
    char *pos = strchr(path, '/');
    if (pos == NULL) {
        char *addr = (char*) malloc(strlen(path) + 1);
        char *file = (char*) malloc(2);
        strncpy(addr, path, strlen(path));
        strncpy(file, "/", 1);
        *file_out = file;
        return addr;
    }
    int len_addr = pos - path;
    int len_file = strlen(path) - (pos - path);
    
    char *addr = (char*) malloc(len_addr + 1);
    char *file = (char*) malloc(len_file + 1);

    strncpy(addr, path, len_addr);
    addr[len_addr] = '\0';

    strcpy(file, pos);

    *file_out = file;
    return addr;
}

int split_port(char *addr) {
    char *pos = strrchr(addr, ':');
    if (pos == NULL) {
        return PORT;
    }
    int port = atoi(pos + 1);
    *pos = '\0';
    return port;
}

void print_current_time() {
    char time_buffer[512];
    time_t raw_time;
    struct tm *current_time;
    time(&raw_time);
    current_time = localtime(&raw_time);
    strftime(time_buffer, 512, "%a, %d %b %Y %T %Z", current_time);
    DLog("[%s] Response received from server\n", time_buffer);
}

long get_http_speed(int test_respond_size, const char * server_address, int port, const char * file_name, struct sockaddr_in address) {
    // 记录请求开始时间
    int response_speed = -1;
    struct timeval time_start;
    gettimeofday(&time_start, NULL);

    // Open up a socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        DLog("Unable to open socket\n"); 
        return -1;
    }

    if (connect(sockfd, (struct sockaddr*) &address, sizeof(address)) < 0) {
        DLog("Unable to connect to host\n");
        return -1;
    }

    DLog("Connected with host: %s\n", inet_ntoa(address.sin_addr));
    
    // Define buffer size
    const int buffer_size = 8096;
    /*******************************************************/
    /*             Begin sending GET request               */
    /*******************************************************/
    char buffer[buffer_size];
    sprintf(buffer, "GET %s HTTP/1.1\r\n", file_name);
    write_socket(sockfd, buffer, strlen(buffer));
    sprintf(buffer, "Host: %s:%d\r\n", server_address, port);
    write_socket(sockfd, buffer, strlen(buffer));
    sprintf(buffer, "User-Agent: Mozilla/5.0 (Windows NT 6.1) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/31.0.1650.16 Safari/537.36\r\n");
    write_socket(sockfd, buffer, strlen(buffer));
    sprintf(buffer, "Connection: close\r\n");
    write_socket(sockfd, buffer, strlen(buffer));

    // Signal end of headers with empty line
    sprintf(buffer, "\r\n");
    write_socket(sockfd, buffer, strlen(buffer));

    /*******************************************************/
    /*             Begin reading response                  */
    /*******************************************************/

    // Read first line and print to console
    int len = read_line(sockfd, buffer, sizeof(buffer));
    if (len <= 0) {
        DLog("No response received from server\n");
        goto out;
    } else {
        print_current_time();
    }

    char version[16];
    char response_code[16];
    char response_reason[256];
    int i, j;

    // Read version
    for(i = 0; i < sizeof(version) - 1 && !isspace(buffer[i]); i++) {
        version[i] = buffer[i];
    }
    version[i] = '\0';

    // Skip over spaces
    for (; isspace(buffer[i]) && i < sizeof(buffer); i++);

    // Read response code
    for (j = 0; i < sizeof(buffer) && j < sizeof(response_code) - 1 && !isspace(buffer[i]); i++, j++) {
        response_code[j] = buffer[i];
    }
    response_code[j] = '\0';

    // Skip over spaces
    for (; isspace(buffer[i]) && i < sizeof(buffer); i++);

    // Read response reason
    for (j = 0; i < sizeof(buffer) && j < sizeof(response_reason) - 1 && buffer[i] != '\n'; i++, j++) {
        response_reason[j] = buffer[i];
    }
    response_reason[j] = '\0';

    // DLog("Version: %s\n", version);
    // DLog("Response Code: %s\n", response_code);
    // DLog("Response Reason: %s\n", response_reason);

    struct HeaderReturnStuct header_return = read_headers(sockfd);

    if (strcmp(response_code, "200") != 0) {
        if (strcmp(response_code, "301") == 0) {
            DLog("%s Error: %s \n", response_code, response_reason);
        } else {
            DLog("%s Error: %s\n", response_code, response_reason);
        }
        goto out;
    }

    if (header_return.header_err_flag) {
        DLog("Error reading headers\n");
        goto out;
    }
    
    // Sometimes, server doesn't return content-length
//    if (header_return.content_length <= 0) {
//         DLog("No content received from server\n");
//         goto out;
//    }

    // lenght in Byte
    long response_length = read_socket(sockfd, buffer, buffer_size, test_respond_size);
    if (response_length <= 0) {
        goto out;
    } else {
        // Calculate the interval time from start to end, and get the internet speed
        struct timeval time_end;
        gettimeofday(&time_end, NULL);
        long miliStartSecond = (time_start.tv_sec)*1000 + (long)time_start.tv_usec/1000;
        long miliEndSecond = (time_end.tv_sec)*1000 + (long)time_end.tv_usec/1000;
        response_speed = response_length/((miliEndSecond - miliStartSecond)/1000.f); // "B/s"

        DLog("millisecond interval: %ld , target size: %d B, response length: %ld B, response speed: %d KB/S\n", miliEndSecond - miliStartSecond, test_respond_size, response_length, response_speed/1024);
    }

out:
    // Close the connection down
    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);
    sockfd = -1;
    return response_speed;
}

char * get_preferred_ip(const char * domainName) {
    // The host address and file we wish to GET, e.g. http://127.0.0.1/blah.txt
    const char *addr_start = domainName;
    // Skip past http:// if it's there
    if (strncasecmp(addr_start, "http://", strlen("http://")) == 0) {
        addr_start += strlen("http://"); // 如果有http开头的，则使指针跳过开头
    }

    // Split the host address into a web address and a file name/path, 
    // e.g. 127.0.0.1/blah.txt into 127.0.0.1 and /blah.txt
    char *file_name;
    char *server_address = split_path(addr_start, &file_name);
    int port = split_port(server_address);

    struct hostent *host_entity;    
    if ((host_entity = gethostbyname(server_address)) == NULL) {
        DLog("gethostbyname failed on '%s'\n", domainName);
        return NULL;
    }

    int i = 0;
    char * fast_ip = NULL;
    int fast_speed = -1;
    while (host_entity->h_addr_list[i] != NULL) {
        struct sockaddr_in address;
        address.sin_family = AF_INET;
        memcpy(&address.sin_addr, host_entity->h_addr_list[i], host_entity->h_length);
        address.sin_port = htons(port);

        int speed = get_http_speed(1024*100, server_address, port, file_name, address);
        if (speed > fast_speed && speed > 0) {
            if (fast_ip != NULL){
                free(fast_ip);
                fast_ip = NULL;
            }
            fast_speed = speed;
            char * tmp = inet_ntoa(address.sin_addr);
            fast_ip = malloc(sizeof(char) * strlen(tmp));
            strcpy(fast_ip, tmp);
        }

        DLog("ip: %s, speed: %d\n\n", inet_ntoa(address.sin_addr), speed);
        i ++;
    }
    return fast_ip;
}

//void close_socket() {
//    if (sockfd > 0) {
//        shutdown(sockfd, SHUT_RDWR);
//        close(sockfd);
//        sockfd = -1;
//    }
//}

//int main(int argc, char **argv) {
//    if (argc != 2) {
//        DLog("use age: XXX http://www.baidu.com \n");
//        return 1;
//    }
//
//    char * fast_ip = get_preferred_ip(argv[1]);
//    if (fast_ip != NULL) {
//        DLog("fast ip : %s \n", fast_ip);
//    }
//    free(fast_ip);
//    return 0;
//}
