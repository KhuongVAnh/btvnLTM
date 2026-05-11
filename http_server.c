#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>

#define SERVER_PORT 8080
#define REQ_BUF_SIZE 2048

static void sigchld_handler(int signo)
{
    (void)signo;
    while (waitpid(-1, NULL, WNOHANG) > 0)
    {
    }
}

int main(void)
{
    signal(SIGCHLD, sigchld_handler);

    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener < 0)
    {
        perror("socket");
        return 1;
    }

    int reuse = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(SERVER_PORT);

    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        close(listener);
        return 1;
    }

    if (listen(listener, 10) < 0)
    {
        perror("listen");
        close(listener);
        return 1;
    }

    printf("HTTP server đang lắng nghe tại cổng %d\n", SERVER_PORT);

    while (1)
    {
        int client = accept(listener, NULL, NULL);
        if (client < 0)
        {
            if (errno == EINTR)
                continue;
            perror("accept");
            continue;
        }

        pid_t pid = fork();
        if (pid < 0)
        {
            perror("fork");
            close(client);
            continue;
        }

        if (pid == 0)
        {
            close(listener);

            char req[REQ_BUF_SIZE + 1];
            int n = recv(client, req, REQ_BUF_SIZE, 0);
            if (n > 0)
            {
                req[n] = '\0';
                printf("[con %d] Yêu cầu nhận được:\n%s\n", (int)getpid(), req);
            }

            const char *body = "<html><body><h1>Xin chào các bạn</h1></body></html>";
            char response[512];
            int body_len = (int)strlen(body);
            int len = snprintf(
                response,
                sizeof(response),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/html; charset=UTF-8\r\n"
                "Content-Length: %d\r\n"
                "Connection: close\r\n"
                "\r\n"
                "%s",
                body_len,
                body);

            send(client, response, len, 0);
            close(client);
            _exit(0);
        }

        close(client);
    }

    close(listener);
    return 0;
}
