/*
 * Server1.c - CS301 Lab 1
 * Single-client TCP server using select().
 *
 * Only one client is served at a time. A second client receives BUSY.
 * Usage: ./Server1 <port>
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <fcntl.h>

#define MAX_INPUT_SIZE 256
#define BUFSIZE 4096
#define MAN_BUFSIZE 16384

static void strip_backspace_formatting(char *text)
{
    size_t read_pos = 0;
    size_t write_pos = 0;

    while (text[read_pos] != '\0') {
        if (text[read_pos + 1] == '\b' && text[read_pos + 2] != '\0') {
            text[write_pos++] = text[read_pos + 2];
            read_pos += 3;
        } else {
            text[write_pos++] = text[read_pos++];
        }
    }
    text[write_pos] = '\0';
}

static void get_man_description(const char *command,
                                char *output, size_t output_size)
{
    while (*command && isspace((unsigned char)*command))
        command++;

    if (*command == '\0') {
        snprintf(output, output_size, "Invalid command name.");
        return;
    }

    /* Accept a command name only; never pass shell syntax to man. */
    for (const char *p = command; *p; p++) {
        if (!isalnum((unsigned char)*p) &&
            *p != '-' && *p != '_' && *p != '.') {
            snprintf(output, output_size, "Invalid command name.");
            return;
        }
    }

    int pipefd[2];
    if (pipe(pipefd) < 0) {
        snprintf(output, output_size, "Internal error: pipe failed.");
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        snprintf(output, output_size, "Internal error: fork failed.");
        return;
    }

    if (pid == 0) {
        close(pipefd[0]);

        if (dup2(pipefd[1], STDOUT_FILENO) < 0)
            _exit(1);
        close(pipefd[1]);

        int nullfd = open("/dev/null", O_WRONLY);
        if (nullfd >= 0) {
            dup2(nullfd, STDERR_FILENO);
            close(nullfd);
        }

        execlp("man", "man", "-P", "cat", command, (char *)NULL);
        _exit(127);
    }

    close(pipefd[1]);

    char man_text[MAN_BUFSIZE];
    size_t total = 0;

    while (total < sizeof(man_text) - 1) {
        ssize_t n = read(pipefd[0], man_text + total,
                         sizeof(man_text) - 1 - total);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (n == 0)
            break;
        total += (size_t)n;
    }

    close(pipefd[0]);
    man_text[total] = '\0';

    int child_status = 0;
    while (waitpid(pid, &child_status, 0) < 0) {
        if (errno != EINTR)
            break;
    }

    if (total == 0 || !WIFEXITED(child_status) ||
        WEXITSTATUS(child_status) != 0) {
        snprintf(output, output_size,
                 "No manual entry found for '%s'.", command);
        return;
    }

    strip_backspace_formatting(man_text);

    char *name = strstr(man_text, "\nNAME");
    if (name == NULL && strncmp(man_text, "NAME", 4) == 0)
        name = man_text;

    if (name != NULL) {
        char *p = strchr(name, '\n');
        if (p != NULL)
            p++;

        while (p != NULL && *p != '\0') {
            char *line_start = p;
            char *line_end = strchr(p, '\n');
            if (line_end == NULL)
                line_end = p + strlen(p);

            char *start = line_start;
            while (start < line_end && isspace((unsigned char)*start))
                start++;

            if (start < line_end) {
                char *end = line_end;
                while (end > start && isspace((unsigned char)end[-1]))
                    end--;

                size_t length = (size_t)(end - start);
                if (length >= output_size)
                    length = output_size - 1;

                memcpy(output, start, length);
                output[length] = '\0';
                return;
            }

            p = (*line_end == '\n') ? line_end + 1 : NULL;
        }
    }

    /* Fallback if the manual page has an unusual layout. */
    size_t length = total < 200 ? total : 200;
    if (length >= output_size)
        length = output_size - 1;

    memcpy(output, man_text, length);
    output[length] = '\0';

    for (size_t i = 0; i < length; i++) {
        if (output[i] == '\n' || output[i] == '\r')
            output[i] = ' ';
    }
}

static int send_all(int fd, const char *buf, size_t length)
{
    size_t sent = 0;
    while (sent < length) {
        ssize_t n = send(fd, buf + sent, length - sent, 0);
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

/* Read one command ending in newline. */
static int receive_command(int fd, char *buffer, size_t size)
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
            return 0;
        if (ch == '\n')
            break;

        buffer[used++] = ch;
    }

    buffer[used] = '\0';
    return 1;
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <server-port>\n", argv[0]);
        return 1;
    }

    char *endptr;
    long parsed_port = strtol(argv[1], &endptr, 10);
    if (*argv[1] == '\0' || *endptr != '\0' ||
        parsed_port < 1 || parsed_port > 65535) {
        fprintf(stderr, "Invalid port number.\n");
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(listen_fd);
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons((unsigned short)parsed_port);

    if (bind(listen_fd, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return 1;
    }

    if (listen(listen_fd, 10) < 0) {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    printf("Server1 listening on port %ld (single-client, select-based)\n",
           parsed_port);
    fflush(stdout);

    int active_fd = -1;

    while (1) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(listen_fd, &read_fds);

        int max_fd = listen_fd;

        if (active_fd != -1) {
            FD_SET(active_fd, &read_fds);
            if (active_fd > max_fd)
                max_fd = active_fd;
        }

        int ready = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            perror("select");
            break;
        }

        /* Handle new connection(s). */
        if (FD_ISSET(listen_fd, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int new_fd = accept(listen_fd,
                                (struct sockaddr *)&client_addr,
                                &client_len);

            if (new_fd < 0) {
                if (errno == EINTR)
                    continue;
                perror("accept");
            } else if (active_fd == -1) {
                active_fd = new_fd;

                if (send_all(active_fd, "READY\n", 6) < 0) {
                    close(active_fd);
                    active_fd = -1;
                } else {
                    printf("Client connected: fd=%d\n", active_fd);
                    fflush(stdout);
                }
            } else {
                send_all(new_fd, "BUSY\n", 5);
                close(new_fd);
                printf("Rejected client: line busy\n");
                fflush(stdout);
            }
        }

        /* Handle command from the one active client. */
        if (active_fd != -1 && FD_ISSET(active_fd, &read_fds)) {
            char command[MAX_INPUT_SIZE];
            memset(command, 0, sizeof(command));

            int result = receive_command(active_fd, command,
                                         sizeof(command));

            if (result <= 0) {
                printf("Client disconnected.\n");
                fflush(stdout);
                close(active_fd);
                active_fd = -1;
                continue;
            }

            printf("Received command: %s\n", command);
            fflush(stdout);

            char response[BUFSIZE];
            memset(response, 0, sizeof(response));
            get_man_description(command, response, sizeof(response));

            if (send_all(active_fd, response, strlen(response)) < 0 ||
                send_all(active_fd, "\n###END###\n", 11) < 0) {
                close(active_fd);
                active_fd = -1;
            }
        }
    }

    if (active_fd != -1)
        close(active_fd);
    close(listen_fd);
    return 0;
}
