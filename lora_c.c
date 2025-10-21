#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>

#define HOST "0.0.0.0"
#define PORT 23
#define MAX_CLIENTS 10
#define MAX_QSO_QUEUE 100
#define BUF_SIZE 1024

int QSObool = 1; // 送信中フラグ (1=送信可能)
char *qsoQueue[MAX_QSO_QUEUE];
int qsoFront = 0, qsoRear = 0;

int clients[MAX_CLIENTS];
int zlog_clients[MAX_CLIENTS];
int client_count = 0;
int zlog_count = 0;

pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;

// ======================== QSOキュー管理 ========================
void QSOqueue(const char *data) {
    pthread_mutex_lock(&queue_mutex);
    if ((qsoRear + 1) % MAX_QSO_QUEUE == qsoFront) {
        printf("QSOキューが満杯です\n");
    } else {
        qsoQueue[qsoRear] = strdup(data);
        qsoRear = (qsoRear + 1) % MAX_QSO_QUEUE;
        printf("QSOをキューに追加: %s\n", data);
    }
    pthread_mutex_unlock(&queue_mutex);
}

char *QSOPop() {
    pthread_mutex_lock(&queue_mutex);
    char *res = NULL;
    if (QSObool && qsoFront != qsoRear) {
        res = qsoQueue[qsoFront];
        qsoFront = (qsoFront + 1) % MAX_QSO_QUEUE;
        QSObool = 0;
    }
    pthread_mutex_unlock(&queue_mutex);
    return res;
}

// ======================== クライアント管理 ========================
void add_client(int sock, int is_zlog) {
    pthread_mutex_lock(&client_mutex);
    if (is_zlog) {
        zlog_clients[zlog_count++] = sock;
        printf("ZLOGクライアントを登録しました\n");
    } else {
        clients[client_count++] = sock;
    }
    pthread_mutex_unlock(&client_mutex);
}

void remove_client(int sock) {
    pthread_mutex_lock(&client_mutex);
    for (int i = 0; i < client_count; i++) {
        if (clients[i] == sock) {
            for (int j = i; j < client_count - 1; j++)
                clients[j] = clients[j + 1];
            client_count--;
            break;
        }
    }
    for (int i = 0; i < zlog_count; i++) {
        if (zlog_clients[i] == sock) {
            for (int j = i; j < zlog_count - 1; j++)
                zlog_clients[j] = zlog_clients[j + 1];
            zlog_count--;
            break;
        }
    }
    pthread_mutex_unlock(&client_mutex);
    close(sock);
    printf("クライアント切断\n");
}

// ======================== ZLOG送信 ========================
void send_zlog_QSO(const char *msg) {
    pthread_mutex_lock(&client_mutex);
    if (zlog_count == 0) {
        printf("ZLOG接続がありません\n");
        pthread_mutex_unlock(&client_mutex);
        return;
    }

    for (int i = 0; i < zlog_count; i++) {
        if (send(zlog_clients[i], msg, strlen(msg), 0) < 0) {
            perror("ZLOG送信失敗");
        }
    }
    pthread_mutex_unlock(&client_mutex);
}

// ======================== クライアント処理 ========================
void *handle_client(void *arg) {
    int sock = *(int *)arg;
    free(arg);
    char buf[BUF_SIZE];

    int recv_len = recv(sock, buf, sizeof(buf) - 1, 0);
    if (recv_len <= 0) {
        remove_client(sock);
        return NULL;
    }
    buf[recv_len] = '\0';

    int is_zlog = (strstr(buf, "ZLOG") != NULL);
    add_client(sock, is_zlog);

    printf("新規接続 (%s): %s\n", is_zlog ? "ZLOG" : "通常", buf);

    while (1) {
        memset(buf, 0, sizeof(buf));
        recv_len = recv(sock, buf, sizeof(buf) - 1, 0);
        if (recv_len <= 0) {
            remove_client(sock);
            break;
        }

        buf[recv_len] = '\0';
        printf("受信: %s\n", buf);

        if (strstr(buf, "PUTQSO")) {
            QSOqueue(buf);
            char *msg = QSOPop();
            if (msg) {
                send_zlog_QSO(msg);
                free(msg);
            }
        } else if (strstr(buf, "confirmation")) {
            QSObool = 1;
        } else if (qsoFront != qsoRear) {
            char *msg = QSOPop();
            if (msg) {
                send_zlog_QSO(msg);
                free(msg);
            }
        } else {
            printf("通常メッセージ: %s\n", buf);
        }
    }

    return NULL;
}

// ======================== メイン ========================
int main() {
    int server_fd, new_sock;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("ソケット作成失敗");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("バインド失敗");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 5) < 0) {
        perror("リッスン失敗");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Telnetサーバ起動中: %s:%d\n", HOST, PORT);

    while (1) {
        new_sock = accept(server_fd, (struct sockaddr *)&addr, &addrlen);
        if (new_sock < 0) {
            perror("接続失敗");
            continue;
        }

        int *sock_ptr = malloc(sizeof(int));
        *sock_ptr = new_sock;

        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, sock_ptr);
        pthread_detach(tid);
    }

    close(server_fd);
    return 0;
}
