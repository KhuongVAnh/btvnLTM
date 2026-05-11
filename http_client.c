#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080
#define BUF_SIZE 2048

static int send_all(int fd, const char *data, size_t len)
{
    size_t sent = 0;
    while (sent < len)
    {
        int n = send(fd, data + sent, len - sent, 0);
        if (n <= 0)
            return -1;
        sent += (size_t)n;
    }
    return 0;
}

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

    char request_line[256];
    printf("Nhập request line (ví dụ: GET / HTTP/1.1).\n");
    printf("Nhấn Enter để dùng mặc định GET / HTTP/1.1: ");
    if (fgets(request_line, sizeof(request_line), stdin) == NULL)
    {
        close(client);
        return 1;
    }

    request_line[strcspn(request_line, "\r\n")] = '\0';
    if (request_line[0] == '\0')
        snprintf(request_line, sizeof(request_line), "GET / HTTP/1.1");

    char request[512];
    snprintf(
        request,
        sizeof(request),
        "%s\r\n"
        "Host: 127.0.0.1\r\n"
        "Connection: close\r\n"
        "\r\n",
        request_line);

    if (send_all(client, request, strlen(request)) < 0)
    {
        perror("send");
        close(client);
        return 1;
    }

    printf("Phản hồi từ server:\n\n");
    while (1)
    {
        char buf[BUF_SIZE + 1];
        int n = recv(client, buf, BUF_SIZE, 0);
        if (n < 0)
        {
            perror("recv");
            close(client);
            return 1;
        }
        if (n == 0)
            break;

        buf[n] = '\0';
        printf("%s", buf);
    }

    close(client);
    return 0;
}
