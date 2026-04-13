#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/select.h>

int main()
{
    int client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client < 0)
    {
        perror("socket");
        return 1;
    }

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    saddr.sin_port = htons(3000);

    int res = connect(client, (struct sockaddr *)&saddr, sizeof(saddr));
    if (res == -1)
    {
        perror("connect");
        close(client);
        return 1;
    }

    fd_set readfds;
    char buf[256];

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

        // có input từ bàn phím
        if (FD_ISSET(STDIN_FILENO, &readfds))
        {
            if (fgets(buf, sizeof(buf), stdin) == NULL)
                break;

            int s = send(client, buf, strlen(buf), 0);
            if (s < 0)
            {
                perror("send");
                break;
            }
        }

        // có dữ liệu từ server
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
                printf("Server disconnected.\n");
                break;
            }

            buf[n] = '\0';
            printf("%s", buf);
        }
    }

    close(client);
    return 0;
}