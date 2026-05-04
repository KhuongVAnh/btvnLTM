/* Simple pub/sub server
   - Listen on port 9000
   - Commands from clients:
       SUB <topic>       : subscribe to topic
       UNSUB <topic>     : unsubscribe from topic
       PUB <topic> <msg> : publish message to topic
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define MAX_CLIENTS 100
#define MAX_SUB_PER_CLIENT 100
#define MAX_TOPIC_LEN 64
#define MAX_MSG_LEN 1024

int clients[MAX_CLIENTS];
int num_clients = 0;
char *client_topics[MAX_CLIENTS][MAX_SUB_PER_CLIENT];
int client_topic_count[MAX_CLIENTS];

// Loại bỏ kí tự xuống dòng cuối chuỗi
static void trim_newline(char *s)
{
    s[strcspn(s, "\r\n")] = '\0';
}

// Tìm index của client trong mảng clients, trả về -1 nếu không tìm thấy
static int find_client_index(int fd)
{
    for (int i = 0; i < num_clients; i++)
    {
        if (clients[i] == fd)
            return i;
    }
    return -1;
}

// Kiểm tra client đã subscribe topic chưa
static int client_has_topic(int idx, const char *topic)
{
    for (int i = 0; i < client_topic_count[idx]; i++)
    {
        if (strcmp(client_topics[idx][i], topic) == 0)
            return 1;
    }
    return 0;
}

// Thêm subscription cho client (nếu chưa có)
static void add_subscription(int idx, const char *topic)
{
    if (client_topic_count[idx] >= MAX_SUB_PER_CLIENT)
        return;
    if (client_has_topic(idx, topic))
        return;
    client_topics[idx][client_topic_count[idx]] = strdup(topic);
    client_topic_count[idx]++;
}

// Xóa subscription của client
static void remove_subscription(int idx, const char *topic)
{
    for (int i = 0; i < client_topic_count[idx]; i++)
    {
        if (strcmp(client_topics[idx][i], topic) == 0)
        {
            free(client_topics[idx][i]);
            // dịch trái phần còn lại
            for (int j = i; j < client_topic_count[idx] - 1; j++)
                client_topics[idx][j] = client_topics[idx][j + 1];
            client_topic_count[idx]--;
            client_topics[idx][client_topic_count[idx]] = NULL;
            return;
        }
    }
}

// Gửi message tới tất cả client đang subscribe topic
static void forward_to_subscribers(const char *topic, const char *msg, int sender_fd)
{
    char buf[MAX_MSG_LEN + MAX_TOPIC_LEN + 32];
    int len = snprintf(buf, sizeof(buf), "[%s] %s\n", topic, msg);

    for (int i = 0; i < num_clients; i++)
    {
        if (client_has_topic(i, topic))
        {
            int tofd = clients[i];
            // Gửi, nếu lỗi thì đóng client
            int r = send(tofd, buf, len, 0);
            if (r == -1)
            {
                perror("send");
                close(tofd);
                // remove client from list
                for (int j = i; j < num_clients - 1; j++)
                {
                    clients[j] = clients[j + 1];
                    // di chuyển danh sách topic
                    memcpy(client_topics[j], client_topics[j + 1], sizeof(client_topics[j]));
                    client_topic_count[j] = client_topic_count[j + 1];
                }
                num_clients--;
                i--; // kiểm tra lại vị trí i sau khi xóa
            }
        }
    }
}

int main()
{
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener < 0)
    {
        perror("socket");
        return 1;
    }

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = htonl(INADDR_ANY);
    saddr.sin_port = htons(9000);

    if (bind(listener, (struct sockaddr *)&saddr, sizeof(saddr)) == -1)
    {
        perror("bind");
        close(listener);
        return 1;
    }

    if (listen(listener, 5) == -1)
    {
        perror("listen");
        close(listener);
        return 1;
    }

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(listener, &readfds);
    int maxfd = listener;

    // Khởi tạo trạng thái client
    num_clients = 0;
    for (int i = 0; i < MAX_CLIENTS; i++)
        client_topic_count[i] = 0;

    printf("Pub/Sub server listening on port 9000\n");

    while (1)
    {
        fd_set tmp = readfds;
        int res = select(maxfd + 1, &tmp, NULL, NULL, NULL);
        if (res == -1)
        {
            perror("select");
            break;
        }

        // Có kết nối mới
        if (FD_ISSET(listener, &tmp))
        {
            int newfd = accept(listener, NULL, NULL);
            if (newfd == -1)
            {
                perror("accept");
            }
            else
            {
                if (num_clients < MAX_CLIENTS)
                {
                    FD_SET(newfd, &readfds);
                    if (newfd > maxfd)
                        maxfd = newfd;
                    clients[num_clients++] = newfd;
                    client_topic_count[num_clients - 1] = 0;
                    printf("New client %d connected\n", newfd);
                    send(newfd, "Welcome to simple pubsub server\n", 30, 0);
                }
                else
                {
                    send(newfd, "Server full\n", 11, 0);
                    close(newfd);
                }
            }
        }

        // Kiểm tra client hiện có
        for (int i = 0; i < num_clients; i++)
        {
            int fd = clients[i];
            if (FD_ISSET(fd, &tmp))
            {
                char buf[2048];
                int n = recv(fd, buf, sizeof(buf) - 1, 0);
                if (n <= 0)
                {
                    // client ngắt kết nối
                    printf("Client %d disconnected\n", fd);
                    close(fd);
                    FD_CLR(fd, &readfds);
                    // giải phóng topic của client
                    for (int k = 0; k < client_topic_count[i]; k++)
                        free(client_topics[i][k]);
                    // dịch trái mảng clients và topics
                    for (int j = i; j < num_clients - 1; j++)
                    {
                        clients[j] = clients[j + 1];
                        memcpy(client_topics[j], client_topics[j + 1], sizeof(client_topics[j]));
                        client_topic_count[j] = client_topic_count[j + 1];
                    }
                    num_clients--;
                    i--;
                    continue;
                }

                buf[n] = '\0';
                trim_newline(buf);

                // Phân tích lệnh: SUB, UNSUB, PUB
                char *cmd = strtok(buf, " \t");
                if (cmd == NULL)
                    continue;

                if (strcmp(cmd, "SUB") == 0)
                {
                    char *topic = strtok(NULL, " \t");
                    if (topic != NULL)
                    {
                        // thêm subscription cho client i
                        add_subscription(i, topic);
                        send(fd, "OK\n", 3, 0);
                    }
                    else
                    {
                        send(fd, "ERR usage: SUB <topic>\n", 24, 0);
                    }
                }
                else if (strcmp(cmd, "UNSUB") == 0)
                {
                    char *topic = strtok(NULL, " \t");
                    if (topic != NULL)
                    {
                        remove_subscription(i, topic);
                        send(fd, "OK\n", 3, 0);
                    }
                    else
                    {
                        send(fd, "ERR usage: UNSUB <topic>\n", 26, 0);
                    }
                }
                else if (strcmp(cmd, "PUB") == 0)
                {
                    char *topic = strtok(NULL, " \t");
                    if (topic != NULL)
                    {
                        // Phần còn lại là message (có thể chứa khoảng trắng)
                        char *msg = strtok(NULL, "");
                        if (msg == NULL)
                            msg = "";
                        // gửi tới tất cả subscribers
                        forward_to_subscribers(topic, msg, fd);
                        send(fd, "OK\n", 3, 0);
                    }
                    else
                    {
                        send(fd, "ERR usage: PUB <topic> <msg>\n", 29, 0);
                    }
                }
                else
                {
                    send(fd, "ERR unknown command\n", 21, 0);
                }
            }
        }
    }

    close(listener);
    return 0;
}
