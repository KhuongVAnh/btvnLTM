#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 9090
#define BUF_SIZE 1024

int main(void)
{
    int client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client < 0)
    {
        perror("socket");
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    if (connect(client, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("connect");
        close(client);
        return 1;
    }

    printf("Đã kết nối tới time_server %s:%d\n", SERVER_IP, SERVER_PORT);
    printf("Nhập: GET_TIME [format]\n");
    printf("Định dạng: dd/mm/yyyy, dd/mm/yy, mm/dd/yyyy, mm/dd/yy\n");
    printf("Nhập exit để thoát\n");

    pid_t pid = fork();
    if (pid < 0)
    {
        perror("fork");
        close(client);
        return 1;
    }

    if (pid == 0)
    {
        char buf[BUF_SIZE];
        while (1)
        {
            if (fgets(buf, sizeof(buf), stdin) == NULL)
                break;

            if (strncmp(buf, "exit", 4) == 0)
                break;

            if (send(client, buf, (int)strlen(buf), 0) < 0)
            {
                perror("send");
                break;
            }
        }

        shutdown(client, SHUT_WR);
        _exit(0);
    }

    while (1)
    {
        char buf[BUF_SIZE + 1];
        int n = recv(client, buf, BUF_SIZE, 0);
        if (n < 0)
        {
            perror("recv");
            break;
        }
        if (n == 0)
            break;

        buf[n] = '\0';
        printf("Server trả về: %s", buf);
    }

    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
    close(client);
    return 0;
}
