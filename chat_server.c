#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/wait.h>

#define SERVER_PORT 9000
#define MAX_CLIENTS 32
#define BUF_SIZE 2048
#define ID_LEN 64
#define NAME_LEN 64

typedef struct
{
    int used;
    int client_fd;
    int pipe_fd;
    pid_t pid;
} worker_t;

static worker_t workers[MAX_CLIENTS];

static void send_text(int fd, const char *text)
{
    send(fd, text, strlen(text), 0);
}

static void trim_spaces(char *s)
{
    char *start = s;
    while (*start && isspace((unsigned char)*start))
        start++;

    char *end = start + strlen(start);
    while (end > start && isspace((unsigned char)end[-1]))
        end--;

    size_t len = (size_t)(end - start);
    if (start != s)
        memmove(s, start, len);
    s[len] = '\0';
}

static int parse_login(const char *line, char *id, size_t id_sz, char *name, size_t name_sz)
{
    char temp[BUF_SIZE];
    snprintf(temp, sizeof(temp), "%s", line);

    char *colon = strchr(temp, ':');
    if (colon == NULL)
        return 0;

    *colon = '\0';
    char *left = temp;
    char *right = colon + 1;
    trim_spaces(left);
    trim_spaces(right);

    if (left[0] == '\0' || right[0] == '\0')
        return 0;
    if (strpbrk(left, " \t") != NULL)
        return 0;
    if (strpbrk(right, " \t") != NULL)
        return 0;

    snprintf(id, id_sz, "%s", left);
    snprintf(name, name_sz, "%s", right);
    return 1;
}

static void close_worker(int i)
{
    if (i < 0 || i >= MAX_CLIENTS || !workers[i].used)
        return;

    if (workers[i].pipe_fd >= 0)
        close(workers[i].pipe_fd);
    if (workers[i].client_fd >= 0)
        close(workers[i].client_fd);

    workers[i].used = 0;
    workers[i].pipe_fd = -1;
    workers[i].client_fd = -1;
    workers[i].pid = -1;
}

static void sigchld_handler(int signo)
{
    (void)signo;
    while (waitpid(-1, NULL, WNOHANG) > 0)
    {
    }
}

static void broadcast(pid_t sender_pid, const char *sender_id, const char *msg)
{
    char out[BUF_SIZE + ID_LEN + 64];
    char timebuf[32] = "";

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (t != NULL)
        strftime(timebuf, sizeof(timebuf), "%Y/%m/%d %I:%M:%S%p", t);

    if (timebuf[0] != '\0')
        snprintf(out, sizeof(out), "%s %s: %s\n", timebuf, sender_id, msg);
    else
        snprintf(out, sizeof(out), "%s: %s\n", sender_id, msg);

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (!workers[i].used || workers[i].pid == sender_pid)
            continue;

        if (send(workers[i].client_fd, out, strlen(out), 0) < 0)
            close_worker(i);
    }
}

/*
 * Tiến trình con:
 * 1) Đọc thông tin đăng nhập đến khi đúng "client_id: client_name"
 * 2) Đọc tin nhắn chat và gửi về cha qua pipe theo dạng "pid|id|message\n"
 */
static void child_loop(int client_fd, int pipe_write)
{
    send_text(client_fd, "Nhập theo đúng định dạng: client_id: client_name\n");

    char linebuf[BUF_SIZE];
    size_t used = 0;
    int logged_in = 0;
    char client_id[ID_LEN] = "";

    while (1)
    {
        char recvbuf[512];
        int n = recv(client_fd, recvbuf, sizeof(recvbuf), 0);
        if (n <= 0)
            break;

        if (used + (size_t)n >= sizeof(linebuf) - 1)
        {
            send_text(client_fd, "Dữ liệu quá dài.\n");
            used = 0;
            linebuf[0] = '\0';
            continue;
        }

        memcpy(linebuf + used, recvbuf, (size_t)n);
        used += (size_t)n;
        linebuf[used] = '\0';

        char *cursor = linebuf;
        while (1)
        {
            char *nl = strchr(cursor, '\n');
            if (nl == NULL)
                break;

            *nl = '\0';
            cursor[strcspn(cursor, "\r")] = '\0';
            trim_spaces(cursor);

            if (cursor[0] != '\0')
            {
                if (!logged_in)
                {
                    char name[NAME_LEN];
                    if (parse_login(cursor, client_id, sizeof(client_id), name, sizeof(name)))
                    {
                        logged_in = 1;
                        char ok[160];
                        snprintf(ok, sizeof(ok), "Đăng ký thành công: %s (%s)\n", client_id, name);
                        send(client_fd, ok, strlen(ok), 0);
                    }
                    else
                    {
                        send_text(client_fd, "Sai cú pháp. Dùng: client_id: client_name\n");
                    }
                }
                else
                {
                    if (strcmp(cursor, "exit") == 0)
                        goto done;

                    char pipe_msg[BUF_SIZE + ID_LEN + 32];
                    snprintf(pipe_msg, sizeof(pipe_msg), "%d|%s|%s\n", (int)getpid(), client_id, cursor);
                    write(pipe_write, pipe_msg, strlen(pipe_msg));
                }
            }

            cursor = nl + 1;
        }

        size_t remain = strlen(cursor);
        memmove(linebuf, cursor, remain + 1);
        used = remain;
    }

done:
    close(client_fd);
    close(pipe_write);
    _exit(0);
}

int main(void)
{
    signal(SIGCHLD, sigchld_handler);

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        workers[i].used = 0;
        workers[i].client_fd = -1;
        workers[i].pipe_fd = -1;
        workers[i].pid = -1;
    }

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

    if (listen(listener, 8) < 0)
    {
        perror("listen");
        close(listener);
        return 1;
    }

    printf("Chat server đang lắng nghe tại cổng %d\n", SERVER_PORT);

    while (1)
    {
        /* Tiến trình cha chờ: kết nối mới HOẶC dữ liệu từ pipe của các tiến trình con */
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(listener, &readfds);

        int maxfd = listener;
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (workers[i].used && workers[i].pipe_fd >= 0)
            {
                FD_SET(workers[i].pipe_fd, &readfds);
                if (workers[i].pipe_fd > maxfd)
                    maxfd = workers[i].pipe_fd;
            }
        }

        int ready = select(maxfd + 1, &readfds, NULL, NULL, NULL);
        if (ready < 0)
        {
            if (errno == EINTR)
                continue;
            perror("select");
            break;
        }

        /* Có client mới: tạo pipe + fork tiến trình con để phục vụ client này */
        if (FD_ISSET(listener, &readfds))
        {
            int client_fd = accept(listener, NULL, NULL);
            if (client_fd >= 0)
            {
                int slot = -1;
                for (int i = 0; i < MAX_CLIENTS; i++)
                {
                    if (!workers[i].used)
                    {
                        slot = i;
                        break;
                    }
                }

                if (slot == -1)
                {
                    send_text(client_fd, "Server đang bận. Thử lại sau.\n");
                    close(client_fd);
                }
                else
                {
                    int pipefd[2];
                    if (pipe(pipefd) < 0)
                    {
                        perror("pipe");
                        close(client_fd);
                    }
                    else
                    {
                        pid_t pid = fork();
                        if (pid < 0)
                        {
                            perror("fork");
                            close(pipefd[0]);
                            close(pipefd[1]);
                            close(client_fd);
                        }
                        else if (pid == 0)
                        {
                            close(listener);
                            close(pipefd[0]);

                            /* Tiến trình con đóng các fd không liên quan được kế thừa từ cha */
                            for (int j = 0; j < MAX_CLIENTS; j++)
                            {
                                if (workers[j].used)
                                {
                                    if (workers[j].client_fd >= 0 && workers[j].client_fd != client_fd)
                                        close(workers[j].client_fd);
                                    if (workers[j].pipe_fd >= 0)
                                        close(workers[j].pipe_fd);
                                }
                            }

                            child_loop(client_fd, pipefd[1]);
                        }
                        else
                        {
                            close(pipefd[1]);
                            workers[slot].used = 1;
                            workers[slot].client_fd = client_fd;
                            workers[slot].pipe_fd = pipefd[0];
                            workers[slot].pid = pid;
                        }
                    }
                }
            }
        }

        /* Nhận dữ liệu từ pipe của con: tách trường rồi phát cho các client khác */
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (!workers[i].used || workers[i].pipe_fd < 0 || !FD_ISSET(workers[i].pipe_fd, &readfds))
                continue;

            char buf[BUF_SIZE + ID_LEN + 32];
            int n = read(workers[i].pipe_fd, buf, sizeof(buf) - 1);
            if (n <= 0)
            {
                close_worker(i);
                continue;
            }

            buf[n] = '\0';

            char *line = strtok(buf, "\n");
            while (line != NULL)
            {
                char *p1 = strchr(line, '|');
                if (p1 != NULL)
                {
                    *p1 = '\0';
                    char *p2 = strchr(p1 + 1, '|');
                    if (p2 != NULL)
                    {
                        *p2 = '\0';
                        pid_t sender_pid = (pid_t)atoi(line);
                        char *sender_id = p1 + 1;
                        char *msg = p2 + 1;
                        if (sender_id[0] != '\0' && msg[0] != '\0')
                            broadcast(sender_pid, sender_id, msg);
                    }
                }

                line = strtok(NULL, "\n");
            }
        }
    }

    close(listener);
    return 0;
}