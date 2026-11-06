/*
 * Client.c - CS301 Lab 1
 * TCP client: sends UNIX command names and displays their man-page descriptions.
 *
 * Usage: ./Client <server-ip> <server-port>
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_INPUT_SIZE 256
#define BUFSIZE 4096
#define RESPONSE_SIZE 16384

static int send_all(int fd, const char *buf, size_t len)
{
    size_t sent = 0;

    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            return -1;
        sent += (size_t)n;
    }
    return 0;
}

/* Read one newline-terminated status/command-independent line. */
static int receive_line(int fd, char *buf, size_t size)
{
    size_t used = 0;

    while (used < size - 1) {
        char ch;
        ssize_t n = recv(fd, &ch, 1, 0);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            return -1;
        if (ch == '\n')
            break;

        buf[used++] = ch;
    }

    buf[used] = '\0';
    return 0;
}

int main(int argc, char *argv[])
{
    int sockfd;
    int port;
    struct sockaddr_in server_addr;
    char input[MAX_INPUT_SIZE];
    char status[32];
    char recvbuf[BUFSIZE];
    char response[RESPONSE_SIZE];

    signal(SIGPIPE, SIG_IGN);

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server-ip> <server-port>\n", argv[0]);
        return 1;
    }

    char *endptr;
    long parsed_port = strtol(argv[2], &endptr, 10);
    if (*argv[2] == '\0' || *endptr != '\0' ||
        parsed_port < 1 || parsed_port > 65535) {
        fprintf(stderr, "Invalid port number.\n");
        return 1;
    }
    port = (int)parsed_port;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((unsigned short)port);

    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid server IP address.\n");
        close(sockfd);
        return 1;
    }

    if (connect(sockfd, (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0) {
        perror("connect");
        close(sockfd);
        return 1;
    }

    /* Server must send READY or BUSY immediately after connection. */
    if (receive_line(sockfd, status, sizeof(status)) < 0) {
        printf("Server disconnected before sending connection status.\n");
        close(sockfd);
        return 1;
    }

    if (strcmp(status, "BUSY") == 0) {
        printf("line busy!\n");
        close(sockfd);
        return 0;
    }

    if (strcmp(status, "READY") != 0) {
        printf("Unexpected server status: %s\n", status);
        close(sockfd);
        return 1;
    }

    printf("Connected to server.\n");

    while (1) {
        printf("Enter UNIX command: ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL)
            break;

        input[strcspn(input, "\r\n")] = '\0';

        if (input[0] == '\0')
            continue;

        /* The newline marks the end of one command in the TCP stream. */
        char command[MAX_INPUT_SIZE + 1];
        int written = snprintf(command, sizeof(command), "%s\n", input);

        if (written < 0 || (size_t)written >= sizeof(command)) {
            printf("Command is too long.\n");
            continue;
        }

        if (send_all(sockfd, command, (size_t)written) < 0) {
            printf("Could not send command. Server may have disconnected.\n");
            break;
        }

        response[0] = '\0';
        size_t response_len = 0;
        int marker_found = 0;

        while (!marker_found) {
            ssize_t n = recv(sockfd, recvbuf, sizeof(recvbuf) - 1, 0);

            if (n < 0) {
                if (errno == EINTR)
                    continue;
                perror("recv");
                close(sockfd);
                return 1;
            }
            if (n == 0) {
                printf("Server disconnected.\n");
                close(sockfd);
                return 0;
            }

            recvbuf[n] = '\0';

            if (response_len + (size_t)n >= sizeof(response)) {
                printf("Server response is too large.\n");
                close(sockfd);
                return 1;
            }

            memcpy(response + response_len, recvbuf, (size_t)n);
            response_len += (size_t)n;
            response[response_len] = '\0';

            char *marker = strstr(response, "###END###");
            if (marker != NULL) {
                *marker = '\0';
                marker_found = 1;
            }
        }

        printf("Server replied:\n%s\n", response);
    }

    close(sockfd);
    return 0;
}
