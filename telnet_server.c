#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 5000
#define MAX_CLIENTS 100
#define BUF_SIZE 1024
#define USER_FILE "users.txt"

// Trạng thái của 1 client
#define STATE_WAIT_USER 0
#define STATE_WAIT_PASS 1
#define STATE_LOGGED_IN 2

typedef struct
{
    int fd;             // socket của client
    int state;          // trạng thái đăng nhập
    char username[100]; // username đã nhập
    char password[100]; // password đã nhập
} ClientInfo;

// Xóa \r \n ở cuối chuỗi
void trim_newline(char *s)
{
    s[strcspn(s, "\r\n")] = '\0';
}

// Kiểm tra user/pass trong file users.txt
int check_login(const char *username, const char *password)
{
    FILE *f = fopen(USER_FILE, "r");
    if (f == NULL)
    {
        perror("fopen users.txt");
        return 0;
    }

    char file_user[100];
    char file_pass[100];

    // Mỗi dòng: user pass
    while (fscanf(f, "%99s %99s", file_user, file_pass) == 2)
    {
        if (strcmp(username, file_user) == 0 && strcmp(password, file_pass) == 0)
        {
            fclose(f);
            return 1;
        }
    }

    fclose(f);
    return 0;
}

// Gửi toàn bộ nội dung file out.txt cho client
void send_file_to_client(int client_fd, const char *filename)
{
    FILE *f = fopen(filename, "r");
    if (f == NULL)
    {
        char *msg = "Khong mo duoc file ket qua.\n";
        send(client_fd, msg, strlen(msg), 0);
        return;
    }

    char buf[BUF_SIZE];
    while (fgets(buf, sizeof(buf), f) != NULL)
    {
        send(client_fd, buf, strlen(buf), 0);
    }

    fclose(f);
}

// Tìm vị trí client trong mảng theo fd
int find_client_index(ClientInfo clients[], int num_clients, int fd)
{
    for (int i = 0; i < num_clients; i++)
    {
        if (clients[i].fd == fd)
            return i;
    }
    return -1;
}

// Xóa client khỏi mảng
void remove_client(ClientInfo clients[], int *num_clients, int index)
{
    close(clients[index].fd);

    // lấy phần tử cuối đè lên để xóa nhanh
    clients[index] = clients[*num_clients - 1];
    (*num_clients)--;
}

int main()
{
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener < 0)
    {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);

    if (bind(listener, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind");
        close(listener);
        return 1;
    }

    if (listen(listener, 5) < 0)
    {
        perror("listen");
        close(listener);
        return 1;
    }

    printf("Server dang lang nghe cong %d...\n", PORT);

    ClientInfo clients[MAX_CLIENTS];
    int num_clients = 0;

    fd_set readfds;

    while (1)
    {
        FD_ZERO(&readfds);
        FD_SET(listener, &readfds);

        int maxfd = listener;

        // thêm tất cả client vào tập theo dõi của select
        for (int i = 0; i < num_clients; i++)
        {
            FD_SET(clients[i].fd, &readfds);
            if (clients[i].fd > maxfd)
                maxfd = clients[i].fd;
        }

        int ret = select(maxfd + 1, &readfds, NULL, NULL, NULL);
        if (ret < 0)
        {
            perror("select");
            break;
        }

        // Có kết nối mới
        if (FD_ISSET(listener, &readfds))
        {
            int new_client = accept(listener, NULL, NULL);
            if (new_client < 0)
            {
                perror("accept");
            }
            else if (num_clients >= MAX_CLIENTS)
            {
                char *msg = "Server day.\n";
                send(new_client, msg, strlen(msg), 0);
                close(new_client);
            }
            else
            {
                clients[num_clients].fd = new_client;
                clients[num_clients].state = STATE_WAIT_USER;
                clients[num_clients].username[0] = '\0';
                clients[num_clients].password[0] = '\0';
                num_clients++;

                char *msg = "Username: ";
                send(new_client, msg, strlen(msg), 0);

                printf("Client moi ket noi: fd=%d\n", new_client);
            }
        }

        // Duyệt các client đang có
        for (int i = 0; i < num_clients; i++)
        {
            int client_fd = clients[i].fd;

            if (!FD_ISSET(client_fd, &readfds))
                continue;

            char buf[BUF_SIZE];
            int bytes_received = recv(client_fd, buf, sizeof(buf) - 1, 0);

            if (bytes_received <= 0)
            {
                printf("Client fd=%d da ngat ket noi\n", client_fd);
                remove_client(clients, &num_clients, i);
                i--;
                continue;
            }

            buf[bytes_received] = '\0';
            trim_newline(buf);

            // Đang chờ username
            if (clients[i].state == STATE_WAIT_USER)
            {
                strcpy(clients[i].username, buf);
                clients[i].state = STATE_WAIT_PASS;

                char *msg = "Password: ";
                send(client_fd, msg, strlen(msg), 0);
            }
            // Đang chờ password
            else if (clients[i].state == STATE_WAIT_PASS)
            {
                strcpy(clients[i].password, buf);

                if (check_login(clients[i].username, clients[i].password))
                {
                    clients[i].state = STATE_LOGGED_IN;
                    char *msg = "Dang nhap thanh cong.\nNhap lenh: ";
                    send(client_fd, msg, strlen(msg), 0);

                    printf("Client fd=%d login thanh cong voi user=%s\n",
                           client_fd, clients[i].username);
                }
                else
                {
                    char *msg = "Sai user/pass. Dang nhap that bai.\nUsername: ";
                    send(client_fd, msg, strlen(msg), 0);

                    // cho nhập lại từ đầu
                    clients[i].state = STATE_WAIT_USER;
                    clients[i].username[0] = '\0';
                    clients[i].password[0] = '\0';
                }
            }
            // Đã đăng nhập, chờ lệnh
            else if (clients[i].state == STATE_LOGGED_IN)
            {
                // client gõ exit thì thoát
                if (strcmp(buf, "exit") == 0)
                {
                    char *msg = "Tam biet.\n";
                    send(client_fd, msg, strlen(msg), 0);
                    printf("Client fd=%d thoat\n", client_fd);
                    remove_client(clients, &num_clients, i);
                    i--;
                    continue;
                }

                // Ghép chuỗi kiểu: "<lenh> > out.txt"
                // Theo đề bài dùng system("dir > out.txt")
                char command[BUF_SIZE + 100];
                snprintf(command, sizeof(command), "%s > out.txt", buf);

                int r = system(command);

                if (r == -1)
                {
                    char *msg = "Loi khi thuc thi lenh.\nNhap lenh: ";
                    send(client_fd, msg, strlen(msg), 0);
                }
                else
                {
                    send_file_to_client(client_fd, "out.txt");
                    send(client_fd, "\nNhap lenh: ", 11, 0);
                }
            }
        }
    }

    close(listener);
    return 0;
}