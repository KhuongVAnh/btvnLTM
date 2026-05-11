#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SERVER_PORT 9000
#define BUF_SIZE 2048

static void sender_loop(int client)
{
    char buf[BUF_SIZE];

    while (1)
    {
        if (fgets(buf, sizeof(buf), stdin) == NULL)
            break;

        if (send(client, buf, strlen(buf), 0) < 0)
        {
            if (errno == EINTR)
                continue;
            perror("send");
            break;
        }

        if (strncmp(buf, "exit", 4) == 0)
            break;
    }
}

static void receiver_loop(int client)
{
    char buf[BUF_SIZE + 1];

    while (1)
    {
        int n = recv(client, buf, sizeof(buf) - 1, 0);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            perror("recv");
            break;
        }

        if (n == 0)
        {
            printf("Server đã ngắt kết nối.\n");
            break;
        }

        buf[n] = '\0';
        printf("%s", buf);
        fflush(stdout);
    }
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);

    int client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client < 0)
    {
        perror("socket");
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(SERVER_PORT);

    if (connect(client, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("connect");
        close(client);
        return 1;
    }

    printf("Đã kết nối tới chat server.\n");
    printf("Dòng đầu tiên phải là: client_id: client_name\n");

    pid_t pid = fork();
    if (pid < 0)
    {
        perror("fork");
        close(client);
        return 1;
    }

    if (pid == 0)
    {
        sender_loop(client);
        close(client);
        _exit(0);
    }

    receiver_loop(client);
    close(client);

    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);

    return 0;
}