#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <errno.h>
#include <sys/select.h>
#include <ctype.h>

int isValid(char *str)
{
    int i = 0;

    // bỏ \r hoặc \n ở cuối do nhập từ terminal / recv
    str[strcspn(str, "\r\n")] = '\0';

    // phần bên trái không được rỗng
    if (str[i] == '\0' || str[i] == ':' || isspace((unsigned char)str[i]))
        return 0;

    // đọc client_id: không cho space, không cho :
    while (str[i] != '\0' && str[i] != ':')
    {
        if (isspace((unsigned char)str[i]))
            return 0;
        i++;
    }

    // phải có dấu :
    if (str[i] != ':')
        return 0;
    i++;

    // phải có đúng 1 dấu cách sau :
    if (str[i] != ' ')
        return 0;
    i++;

    // phần bên phải không được rỗng
    if (str[i] == '\0')
        return 0;

    // client_name không được có khoảng trắng
    while (str[i] != '\0')
    {
        if (isspace((unsigned char)str[i]))
            return 0;
        i++;
    }

    return 1;
}

int main()
{
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in saddr;
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = htonl(INADDR_ANY);
    saddr.sin_port = htons(3000);
    bind(listener, (struct sockaddr *)&saddr, sizeof(saddr));
    listen(listener, 5);

    fd_set readfds;
    int clients[100];
    int num_clients = 0;
    FD_ZERO(&readfds);
    FD_SET(listener, &readfds);
    char *clientNames[100]; // Mảng lưu tên client tương ứng với clients[i]
    int maxfd = listener;
    fd_set validClients;
    FD_ZERO(&validClients);

    while (1)
    {
        fd_set tmpfds = readfds;
        int res = select(maxfd + 1, &tmpfds, NULL, NULL, NULL);
        if (res == -1)
        {
            perror("select");
            break;
        }

        if (FD_ISSET(listener, &tmpfds))
        {
            int new_client = accept(listener, NULL, NULL);
            if (new_client == -1)
            {
                perror("accept");
                continue;
            }
            FD_SET(new_client, &readfds);
            if (new_client > maxfd)
            {
                maxfd = new_client;
            }
            clients[num_clients++] = new_client;
            printf("New client connected: %d\n", new_client);
            send(new_client, "Please enter your name in the format <client_id>: <client_name>\n\0", 100, 0);
        }

        for (int i = 0; i < num_clients; i++)
        {
            int client_fd = clients[i];
            if (FD_ISSET(client_fd, &tmpfds))
            {
                if (FD_ISSET(client_fd, &validClients)) // Nếu client đã được xác thực, nhận tin nhắn bình thường
                {
                    char buf[256];
                    int bytes_received = recv(client_fd, buf, sizeof(buf), 0);
                    if (bytes_received <= 0)
                    {
                        close(client_fd); // Đóng kết nối nếu client ngắt kết nối hoặc có lỗi
                        printf("Client disconnected: %d\n", client_fd);
                        FD_CLR(client_fd, &readfds);
                        FD_CLR(client_fd, &validClients);
                        clients[i] = clients[--num_clients]; // Xóa client khỏi danh sách
                    }
                    else
                    {
                        buf[bytes_received] = '\0';
                        // loa phường đến các client khác mes này
                        for (int j = 0; j < num_clients; j++)
                        {
                            if (clients[j] != client_fd && FD_ISSET(clients[j], &validClients))
                            {
                                char msg[512];
                                int mes = snprintf(msg, sizeof(msg), "%s: %s", clientNames[i], buf);
                                int r = send(clients[j], msg, mes, 0);
                                if (r == -1)
                                {
                                    perror("send");
                                    close(clients[j]);
                                    printf("Client disconnected: %d\n", clients[j]);
                                    FD_CLR(client_fd, &readfds);
                                    FD_CLR(client_fd, &validClients);
                                    free(clientNames[i]);

                                    clients[i] = clients[num_clients - 1];
                                    clientNames[i] = clientNames[num_clients - 1];
                                    num_clients--;
                                    i--; // Xóa client khỏi danh sách
                                }
                            }
                        }
                    }
                }
                else // Nếu client chưa được xác thực, chỉ nhận tên và kiểm tra định dạng
                {
                    char buf[256];
                    int bytes_received = recv(client_fd, buf, sizeof(buf), 0);
                    if (bytes_received <= 0)
                    {
                        close(client_fd); // Đóng kết nối nếu client ngắt kết nối hoặc có lỗi
                        printf("Client disconnected: %d\n", client_fd);
                        FD_CLR(client_fd, &readfds);
                        FD_CLR(client_fd, &validClients);
                        free(clientNames[i]);

                        clients[i] = clients[num_clients - 1];
                        clientNames[i] = clientNames[num_clients - 1];
                        num_clients--;
                        i--; // Xóa client khỏi danh sách
                    }
                    else
                    {
                        buf[bytes_received] = '\0';
                        if (isValid(buf))
                        {
                            printf("client %d: name: %s\n", client_fd, buf);
                            FD_SET(client_fd, &validClients);
                            clientNames[i] = strdup(buf); // Lưu tên client, strdup để cấp phát bộ nhớ cho tên client
                            int r = send(client_fd, "Welcome to the chat server!\n", 29, 0);
                        }
                        else
                        {
                            int r = send(client_fd, "Invalid format. Expected: <client_id>: <client_name>\n", 60, 0);
                            if (r == -1)
                            {
                                perror("send");
                                close(client_fd);
                                printf("Client disconnected: %d\n", client_fd);
                                FD_CLR(client_fd, &readfds);
                                clients[i] = clients[--num_clients]; // Xóa client khỏi danh sách
                            }
                        }
                    }
                }
            }
        }
    }
    close(listener);
}