#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 5000
#define BUF_SIZE 1024

int main()
{
    int client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client < 0)
    {
        perror("socket");
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    server_addr.sin_port = htons(SERVER_PORT);

    if (connect(client, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("connect");
        close(client);
        return 1;
    }

    printf("Da ket noi toi server %s:%d\n", SERVER_IP, SERVER_PORT);

    fd_set readfds;
    char buf[BUF_SIZE];

    while (1)
    {
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(client, &readfds);

        int maxfd = (client > STDIN_FILENO) ? client : STDIN_FILENO;

        int ret = select(maxfd + 1, &readfds, NULL, NULL, NULL);
        if (ret < 0)
        {
            perror("select");
            break;
        }

        // Có dữ liệu từ server
        if (FD_ISSET(client, &readfds))
        {
            int n = recv(client, buf, sizeof(buf) - 1, 0);
            if (n < 0)
            {
                perror("recv");
                break;
            }
            if (n == 0)
            {
                printf("Server da dong ket noi.\n");
                break;
            }

            buf[n] = '\0';
            printf("%s", buf);
            fflush(stdout);
        }

        // Người dùng nhập từ bàn phím
        if (FD_ISSET(STDIN_FILENO, &readfds))
        {
            if (fgets(buf, sizeof(buf), stdin) == NULL)
                break;

            if (send(client, buf, strlen(buf), 0) < 0)
            {
                perror("send");
                break;
            }
        }
    }

    close(client);
    return 0;
}