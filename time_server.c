#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>

#define SERVER_PORT 9090
#define BACKLOG 10
#define BUF_SIZE 1024

static void sigchld_handler(int signo)
{
    (void)signo;
    while (waitpid(-1, NULL, WNOHANG) > 0)
    {
    }
}

static void trim_crlf(char *s)
{
    s[strcspn(s, "\r\n")] = '\0';
}

static const char *get_strftime_format(const char *fmt)
{
    if (strcmp(fmt, "dd/mm/yyyy") == 0)
        return "%d/%m/%Y";
    if (strcmp(fmt, "dd/mm/yy") == 0)
        return "%d/%m/%y";
    if (strcmp(fmt, "mm/dd/yyyy") == 0)
        return "%m/%d/%Y";
    if (strcmp(fmt, "mm/dd/yy") == 0)
        return "%m/%d/%y";
    return NULL;
}

static void handle_client(int client_fd)
{
    char buf[BUF_SIZE + 1];

    while (1)
    {
        int n = recv(client_fd, buf, BUF_SIZE, 0);
        if (n <= 0)
            break;

        buf[n] = '\0';
        trim_crlf(buf);

        char *cmd = strtok(buf, " \t");
        char *fmt = strtok(NULL, " \t");
        char *extra = strtok(NULL, " \t");

        if (cmd == NULL || fmt == NULL || extra != NULL || strcmp(cmd, "GET_TIME") != 0)
        {
            const char *msg =
                "ERR Lệnh không hợp lệ. Dùng: GET_TIME [format]\n"
                "Định dạng hỗ trợ: dd/mm/yyyy, dd/mm/yy, mm/dd/yyyy, mm/dd/yy\n";
            send(client_fd, msg, (int)strlen(msg), 0);
            continue;
        }

        const char *time_fmt = get_strftime_format(fmt);
        if (time_fmt == NULL)
        {
            const char *msg =
                "ERR Định dạng không hỗ trợ. Hỗ trợ: dd/mm/yyyy, dd/mm/yy, mm/dd/yyyy, mm/dd/yy\n";
            send(client_fd, msg, (int)strlen(msg), 0);
            continue;
        }

        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char out[128];

        if (t == NULL)
        {
            const char *msg = "ERR Không lấy được thời gian hệ thống\n";
            send(client_fd, msg, (int)strlen(msg), 0);
            continue;
        }

        strftime(out, sizeof(out), time_fmt, t);
        strncat(out, "\n", sizeof(out) - strlen(out) - 1);
        send(client_fd, out, (int)strlen(out), 0);
    }

    close(client_fd);
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

    if (listen(listener, BACKLOG) < 0)
    {
        perror("listen");
        close(listener);
        return 1;
    }

    printf("time_server đang lắng nghe tại cổng %d\n", SERVER_PORT);

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
            handle_client(client);
            _exit(0);
        }

        close(client);
    }

    close(listener);
    return 0;
}
