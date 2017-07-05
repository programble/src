#if 0
cc -Wall -Wextra -pedantic $@ -o $(dirname $0)/pbd $0 && \
cc -Wall -Wextra -pedantic -DPBCOPY $@ -o $(dirname $0)/pbcopy $0 && \
exec cc -Wall -Wextra -pedantic -DPBCOPY -DPBPASTE $@ -o $(dirname $0)/pbpaste $0
#endif

// TCP server which pipes between macOS pbcopy and pbpaste, and pbcopy and
// pbpaste implementations which connect to it.

#include <arpa/inet.h>
#include <err.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sysexits.h>
#include <unistd.h>

#ifndef PBCOPY

static void spawn(const char *cmd, int childFd, int parentFd) {
    pid_t pid = fork();
    if (pid < 0) err(EX_OSERR, "fork");

    if (pid) {
        int status;
        pid_t wait = waitpid(pid, &status, 0);
        if (wait < 0) err(EX_OSERR, "waitpid");

        if (status) {
            warnx("child %s status %d", cmd, status);
        }
    } else {
        int fd = dup2(parentFd, childFd);
        if (fd < 0) err(EX_OSERR, "dup2");

        int error = execlp(cmd, cmd);
        if (error) err(EX_OSERR, "execlp");
    }
}

int main() {
    int error;

    int server = socket(PF_INET, SOCK_STREAM, 0);
    if (server < 0) err(EX_OSERR, "socket");

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(7062),
        .sin_addr = { .s_addr = htonl(0x7f000001) },
    };

    error = bind(server, (struct sockaddr *)&addr, sizeof(addr));
    if (error) err(EX_OSERR, "bind");

    error = listen(server, 1);
    if (error) err(EX_OSERR, "listen");

    for (;;) {
        int client = accept(server, NULL, NULL);
        if (client < 0) err(EX_OSERR, "accept");

        spawn("pbpaste", STDOUT_FILENO, client);

        uint8_t p;
        ssize_t peek = recv(client, &p, 1, MSG_PEEK);
        if (peek < 0) err(EX_IOERR, "recv");

        if (peek) {
            spawn("pbcopy", STDIN_FILENO, client);
        }

        error = close(client);
        if (error) err(EX_IOERR, "close");
    }
}

#else

int main() {
    int error;

    int client = socket(PF_INET, SOCK_STREAM, 0);
    if (client < 0) err(EX_OSERR, "socket");

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(7062),
        .sin_addr = { .s_addr = htonl(0x7f000001) },
    };

    error = connect(client, (struct sockaddr *)&addr, sizeof(addr));
    if (error) err(EX_OSERR, "connect");

#ifdef PBPASTE
    int fdIn = client;
    int fdOut = STDOUT_FILENO;
    error = shutdown(client, SHUT_WR);
    if (error) err(EX_OSERR, "shutdown");
#else
    int fdIn = STDIN_FILENO;
    int fdOut = client;
#endif

    char readBuf[4096];
    ssize_t readLen;
    while (0 < (readLen = read(fdIn, readBuf, sizeof(readBuf)))) {
        char *writeBuf = readBuf;
        ssize_t writeLen;
        while (0 < (writeLen = write(fdOut, writeBuf, readLen))) {
            writeBuf += writeLen;
            readLen -= writeLen;
        }
        if (writeLen < 0) err(EX_IOERR, "write");
    }
    if (readLen < 0) err(EX_IOERR, "read");

    return EX_OK;
}

#endif
