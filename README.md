# CS301 — Computer Networks
## Lab 1: UNIX Client-Server Socket Programming

**Student:** Varuna Kumari 
**Course:** CS301 — Computer Networks  
**Lab:** Lab 1  

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [Objectives](#2-objectives)
3. [Project Structure](#3-project-structure)
4. [How Client-Server Communication Works](#4-how-client-server-communication-works)
5. [Server1 — Single-Process Server using select()](#5-server1--single-process-server-using-select)
6. [Server2 — Multi-Process Server using fork()](#6-server2--multi-process-server-using-fork)
7. [Difference Between Server1 and Server2](#7-difference-between-server1-and-server2)
8. [Explanation of select()](#8-explanation-of-select)
9. [Explanation of fork()](#9-explanation-of-fork)
10. [Command Explanation Logic](#10-command-explanation-logic)
11. [Security — Input Validation](#11-security--input-validation)
12. [Message Protocol](#12-message-protocol)
13. [Required Software and Dependencies](#13-required-software-and-dependencies)
14. [Compilation](#14-compilation)
15. [Execution](#15-execution)
16. [Sample Output](#16-sample-output)
17. [Testing Multiple Clients](#17-testing-multiple-clients)
18. [Error Handling](#18-error-handling)

---

## 1. Introduction

This lab implements a **UNIX client-server application** using **TCP socket programming in C**.

The client connects to a server and sends UNIX/Linux command names (such as `ls`, `cat`, `pwd`). The server looks up each command's manual page using the `man` command, extracts a short description from the **NAME** section, and returns it to the client.

Two server versions are provided:

| Server | Mechanism | Behaviour |
|--------|-----------|-----------|
| `Server1` | `select()` — single process | Serves one client at a time; rejects others with **"line busy!"** |
| `Server2` | `fork()` — multi-process | Serves many clients simultaneously, each in its own child process |

---

## 2. Objectives

- Understand TCP socket programming using UNIX socket APIs.
- Learn how to create, bind, listen, accept, connect, send, and receive on sockets.
- Implement a **single-process** server using `select()` for I/O multiplexing.
- Implement a **multi-process** server using `fork()` for concurrent client handling.
- Safely retrieve system information using `fork()` + `exec()` + pipes.
- Apply **input validation** to prevent shell injection attacks.
- Handle partial sends, client disconnection, and zombie processes correctly.

---

## 3. Project Structure

```
lab-01/
├── src/
│   ├── Client.c     — TCP client
│   ├── Server1.c    — Single-process server (select-based)
│   └── Server2.c    — Multi-process server (fork-based)
└── README.md
```

---

## 4. How Client-Server Communication Works

```
  CLIENT                                 SERVER
  ------                                 ------
  socket()     ──── TCP connection ────► socket()
  connect()                              bind()
                                         listen()
                                         accept()
                ◄──── "READY\n" ─────── send()

  send("ls\n") ──── command ──────────► recv()
                                         [man ls → extract NAME]
               ◄──── description ─────── send()
               ◄──── "###END###" ──────── send()

  ... repeats until client disconnects (Ctrl+D)
```

**Step-by-step:**

1. The server starts, creates a TCP socket, binds it to a port, and calls `listen()`.
2. The client creates a socket and calls `connect()` with the server's IP and port.
3. After `accept()`, the server immediately sends either `READY` (client accepted) or `BUSY` (line busy — Server1 only).
4. The client enters a loop: reads a command from the user, sends it to the server.
5. The server receives the command, calls `man <command>`, parses the NAME section, and sends the description back.
6. The server appends `###END###` as a terminator so the client knows the response is complete.
7. The client accumulates received data until it finds `###END###`, strips it, and displays the result.
8. The loop continues until the user presses **Ctrl+D** (EOF on stdin).

---

## 5. Server1 — Single-Process Server using select()

**File:** `src/Server1.c`

Server1 is a **single-process** server that can handle **only one client at a time**.

### How it works:

```
listen_fd = socket(...)
bind(listen_fd, port)
listen(listen_fd, 10)
active_fd = -1              ← no active client yet

loop:
    FD_ZERO(&read_fds)
    FD_SET(listen_fd)
    if (active_fd != -1)
        FD_SET(active_fd)   ← also watch active client

    select(...)             ← block until something happens

    if listen_fd is ready:
        new_fd = accept()
        if active_fd == -1:
            active_fd = new_fd
            send(active_fd, "READY\n")
        else:
            send(new_fd, "BUSY\n")
            close(new_fd)   ← reject second client

    if active_fd is ready:
        n = recv(active_fd, command)
        if n == 0: client disconnected → active_fd = -1
        else: get_man_description(command) → send reply + "###END###"
```

### Key points:
- `select()` watches **both** the listening socket and the active client simultaneously in one process.
- Because `select()` is used, the server **never blocks** on the listening socket while serving a client. This is what allows it to detect and reject a second connection with "line busy!" in real time.
- When the active client disconnects, `active_fd` is reset to `-1`, and the next incoming client is accepted normally.
- No threads, no child processes — everything runs in a single process.

---

## 6. Server2 — Multi-Process Server using fork()

**File:** `src/Server2.c`

Server2 is a **multi-process** server that can handle **many clients at the same time**, each in its own child process.

### How it works:

```
Install SIGCHLD handler (to reap zombie children)

listen_fd = socket(...)
bind(listen_fd, port)
listen(listen_fd, 10)

loop:
    client_fd = accept(listen_fd)   ← blocks until a client connects

    pid = fork()

    if pid == 0:  (CHILD process)
        close(listen_fd)            ← child does not need listener
        send(client_fd, "READY\n")
        loop:
            recv command from client
            get_man_description(command) → send reply + "###END###"
        close(client_fd)
        _exit(0)

    if pid > 0:  (PARENT process)
        close(client_fd)            ← parent does not need this socket
        (go back to accept next client)
```

### SIGCHLD handler (zombie prevention):

```c
static void sigchld_handler(int signal_number) {
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;   // reap all finished children
}
```

Installed with `sigaction()` using `SA_RESTART` so that interrupted system calls are automatically restarted.

### Key points:
- Each `fork()` creates a **new process** that serves one client independently.
- The parent immediately closes its copy of `client_fd` and returns to `accept()`.
- The child immediately closes its copy of `listen_fd` and enters the client-serving loop.
- Multiple clients run **truly in parallel** because each is a separate OS process.
- `SIGCHLD` with `waitpid(-1, NULL, WNOHANG)` ensures finished child processes are reaped immediately, preventing zombie processes.

---

## 7. Difference Between Server1 and Server2

| Feature | Server1 | Server2 |
|---------|---------|---------|
| Mechanism | `select()` — I/O multiplexing | `fork()` — multiple processes |
| Concurrent clients | ❌ One at a time | ✅ Unlimited simultaneously |
| Second client behaviour | Receives `line busy!` | Accepted and served in parallel |
| Processes created | 1 (single process) | 1 parent + 1 child per client |
| Memory sharing | Shared (same process) | No sharing (separate address spaces) |
| Zombie prevention | Not needed (no children) | `SIGCHLD` + `waitpid()` |
| Complexity | Simpler | More complex |
| Use case | Simple, low-traffic servers | High-traffic, concurrent servers |

---

## 8. Explanation of select()

`select()` is a UNIX system call that allows a **single process** to monitor multiple file descriptors simultaneously and find out which ones are ready for reading, writing, or have an error.

### Function signature:
```c
int select(int nfds, fd_set *readfds, fd_set *writefds,
           fd_set *exceptfds, struct timeval *timeout);
```

### How it is used in Server1:

```c
fd_set read_fds;
FD_ZERO(&read_fds);           // clear the set
FD_SET(listen_fd, &read_fds); // watch for new connections
FD_SET(active_fd, &read_fds); // watch for data from active client

select(max_fd + 1, &read_fds, NULL, NULL, NULL); // wait

if (FD_ISSET(listen_fd, &read_fds))  // new connection arrived
if (FD_ISSET(active_fd, &read_fds))  // client sent a command
```

### Why select() is needed in Server1:

Without `select()`, the server would have to choose between:
- Blocking on `accept()` — unable to receive commands from the active client.
- Blocking on `recv()` — unable to detect new connections and send "line busy!".

`select()` solves this by watching both sockets at once and waking up only when one of them has data, without ever blocking on just one.

---

## 9. Explanation of fork()

`fork()` is a UNIX system call that creates a **new child process** as an exact copy of the calling parent process.

### Function signature:
```c
pid_t fork(void);
```

### Return values:
- Returns `0` in the **child** process.
- Returns the child's **PID** (> 0) in the **parent** process.
- Returns `-1` on failure.

### How it is used in Server2:

```c
int client_fd = accept(listen_fd, ...);

pid_t pid = fork();

if (pid == 0) {
    // CHILD: serve this client
    close(listen_fd);
    handle_client(client_fd);  // loops receiving commands
    _exit(0);
}
// PARENT: close client socket, go back to accept()
close(client_fd);
```

After `fork()`:
- The **parent** has two sockets open: `listen_fd` and `client_fd`. It closes `client_fd` (the child owns it now) and loops back to `accept()`.
- The **child** has two sockets open: `listen_fd` and `client_fd`. It closes `listen_fd` (it doesn't need to accept new clients) and serves its client.

### Why fork() enables concurrency:

The operating system schedules the parent and all children independently. Each child runs its own `recv()` → `man` → `send()` loop without interfering with any other child. This gives true parallelism without threads.

---

## 10. Command Explanation Logic

The `get_man_description()` function is the core of both servers. It safely retrieves a command's description from its manual page.

### Steps:

**1. Validate the command name**
```c
for (const char *p = command; *p; p++) {
    if (!isalnum(*p) && *p != '-' && *p != '_' && *p != '.')
        // reject — contains illegal character
}
```

**2. Create a pipe and fork a child**
```c
pipe(pipefd);
pid = fork();
```

**3. Child executes `man` with stdout redirected into the pipe**
```c
dup2(pipefd[1], STDOUT_FILENO);  // stdout → pipe write end
execlp("man", "man", "-P", "cat", command, NULL);
```
`-P cat` tells man to use `cat` as its pager, so output is plain text (no interactive terminal control sequences).

**4. Parent reads man output from pipe**
```c
read(pipefd[0], man_text, sizeof(man_text));
waitpid(pid, &status, 0);
```

**5. Strip backspace formatting**
Man pages use `char\bchar` sequences for bold text (e.g., `l\bls\bs`). These are stripped character by character.

**6. Find the NAME section and extract the description**
```
NAME
       ls - list directory contents    ← this line is extracted
```

**7. Send the result back to the client**

### Example results:

| Command | Description returned |
|---------|---------------------|
| `ls` | `ls - list directory contents` |
| `cat` | `cat - concatenate files and print on the standard output` |
| `pwd` | `pwd - print name of current/working directory` |
| `grep` | `grep, egrep, fgrep, rgrep - print lines that match patterns` |
| `date` | `date - print or set the system date and time` |
| `whoami` | `whoami - print effective user name` |
| `notacommand` | `No manual entry found for 'notacommand'.` |
| `ls;rm` | `Invalid command name.` |

---

## 11. Security — Input Validation

The server **never** passes user input directly to a shell. Instead:

1. The command name is validated character by character — only `[a-zA-Z0-9._-]` is allowed.
2. `execlp("man", "man", "-P", "cat", command, NULL)` is used — this passes the command as a **direct argument** to `man`, not through any shell (`sh -c`), so shell injection is impossible even if validation were bypassed.
3. Shell metacharacters like `;`, `|`, `$`, `>`, `<`, space are all rejected immediately.

```
Input: "ls;rm -rf /"  →  Invalid command name.   (semicolon rejected)
Input: "cat /etc/passwd" →  Invalid command name. (space rejected)
Input: "$(whoami)"    →  Invalid command name.   ($ rejected)
```

---

## 12. Message Protocol

A simple text-based protocol is used over TCP:

### Connection handshake (immediately after accept):
```
Server → Client:   "READY\n"   (slot is free, you are accepted)
Server → Client:   "BUSY\n"    (another client is active — Server1 only)
```

### Command/response exchange (repeating loop):
```
Client → Server:   "<command>\n"           (e.g., "ls\n")
Server → Client:   "<description>\n###END###\n"
```

The `###END###` marker tells the client that the full response has arrived. This handles TCP fragmentation — responses may arrive in multiple `recv()` calls, so the client accumulates chunks until it finds the marker.

---

## 13. Required Software and Dependencies

| Software | Purpose | Install command (Ubuntu) |
|----------|---------|--------------------------|
| `gcc` | C compiler | `sudo apt-get install gcc` |
| `man-db` | Manual page database | `sudo apt-get install man-db` |
| `manpages` | UNIX command manual pages | `sudo apt-get install manpages` |

**Operating System:** Linux (Ubuntu 24.04 recommended)  
**Windows users:** Use WSL (Windows Subsystem for Linux)

Install everything at once:
```bash
sudo apt-get update
sudo apt-get install gcc man-db manpages -y
```

---

## 14. Compilation

Navigate to the `src/` directory and compile with GCC:

```bash
cd src/
gcc -Wall Client.c  -o Client
gcc -Wall Server1.c -o Server1
gcc -Wall Server2.c -o Server2
```

All three files compile with **zero errors and zero warnings** using `-Wall` on Ubuntu 24.04.

---

## 15. Execution

### Start Server1 (single-client):
```bash
./Server1 5555
```

### Start Server2 (multi-client):
```bash
./Server2 5555
```

### Start the Client (in a separate terminal):
```bash
./Client 127.0.0.1 5555
```

Where:
- `127.0.0.1` is the server's IP address (localhost)
- `5555` is the port number

Press **Ctrl+D** to exit the client. Press **Ctrl+C** to stop either server.

---

## 16. Sample Output

### Client:
```
$ ./Client 127.0.0.1 5555
Connected to server.
Enter UNIX command: ls
Server replied:
ls - list directory contents

Enter UNIX command: cat
Server replied:
cat - concatenate files and print on the standard output

Enter UNIX command: pwd
Server replied:
pwd - print name of current/working directory

Enter UNIX command: date
Server replied:
date - print or set the system date and time

Enter UNIX command: grep
Server replied:
grep, egrep, fgrep, rgrep - print lines that match patterns

Enter UNIX command: notacommand
Server replied:
No manual entry found for 'notacommand'.

Enter UNIX command: ls;rm
Server replied:
Invalid command name.
```

### Server1:
```
$ ./Server1 5555
Server1 listening on port 5555 (single-client, select-based)
Client connected: fd=4
Received command: ls
Received command: cat
Received command: pwd
Client disconnected.
Client connected: fd=4
Rejected client: line busy
```

### Server2:
```
$ ./Server2 5555
Server2 listening on port 5555 (multi-process, fork-based)
Created child process 1821 for client
Created child process 1822 for client
Created child process 1823 for client
Child PID 1821 received command: ls
Child PID 1822 received command: grep
Child PID 1823 received command: whoami
Child PID 1823: client disconnected
Child PID 1821: client disconnected
Child PID 1822: client disconnected
```

---

## 17. Testing Multiple Clients

Open each of the following in a **separate WSL/Linux terminal**.

### Test 1 — Server1, sequential clients

| Terminal 1 | Terminal 2 |
|------------|------------|
| `./Server1 5555` | |
| | `./Client 127.0.0.1 5555` → type `ls`, press Ctrl+D |
| | `./Client 127.0.0.1 5555` → type `pwd`, press Ctrl+D |

### Test 2 — Server1, line busy rejection

| Terminal 1 | Terminal 2 | Terminal 3 |
|------------|------------|------------|
| `./Server1 5555` | | |
| | `./Client 127.0.0.1 5555` (keep running) | |
| | | `./Client 127.0.0.1 5555` |
| | | Expected: `line busy!` |

### Test 3 — Server2, three simultaneous clients

| Terminal 1 | Terminal 2 | Terminal 3 | Terminal 4 |
|------------|------------|------------|------------|
| `./Server2 5555` | | | |
| | `./Client 127.0.0.1 5555` | `./Client 127.0.0.1 5555` | `./Client 127.0.0.1 5555` |
| | type `ls` | type `grep` | type `whoami` |

All three clients receive their responses independently and simultaneously.

---

## 18. Error Handling

| Situation | How it is handled |
|-----------|------------------|
| Client sends invalid command (e.g., `ls;rm`) | Server replies `Invalid command name.` |
| Client sends unknown command (e.g., `notacommand`) | Server replies `No manual entry found for 'notacommand'.` |
| Client disconnects unexpectedly | `recv()` returns 0 → server closes socket and resets |
| `send()` fails mid-response | `send_all()` retries on `EINTR`, returns error on failure |
| TCP fragmentation (partial recv) | Client accumulates chunks in a buffer until `###END###` is found |
| Port already in use on restart | `SO_REUSEADDR` is set so the server can bind immediately |
| Zombie child processes (Server2) | `SIGCHLD` handler calls `waitpid(-1, NULL, WNOHANG)` in a loop |
| Second client connects to Server1 | Server sends `BUSY\n`, client prints `line busy!`, connection closed |
| `man` not installed | Server replies `No manual entry found for '<cmd>'.` |
