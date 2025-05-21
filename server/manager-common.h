#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdatomic.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <endian.h>
#include <arpa/inet.h>
#include <time.h>
#include <math.h>
#include "manager.h"
#include <netdb.h>



enum ERROR_CODE {
    EFUNCID = 1,
    EVALUE = 2,
};

double get_max_derivate_2(FUNC_TABLE func_id, double left, double right) {
    switch (func_id)
    {
    case EXP:
        return exp(right);
    case SIN:
        if (right - left >= M_PI) {
            return 1;
        }
        double k_left = floor((left - M_PI_2) / M_PI);
        double k_right = floor((right - M_PI_2) / M_PI);
        if (k_left != k_right) {
            return 1;
        }
        return fabs(sin(right)) > fabs(sin(left)) ? fabs(sin(right)) : fabs(sin(left));
    case SQR:
        return 1;
    case NOT_SUPPORT:
    default:
        fprintf(stderr, "[get_max_derivate_2]:Function not supported\n");
        return 0;
    }
}

struct node_info 
{
    time_t max_worker_time;
    int n_cores;
};

struct worker_data{
int func_id; 
double left;
double step;
uint64_t num_steps;
};







typedef enum
{
    CONNECTION_EMPTY,
    GET_INFO,
    SEND_TASK,
    GET_ANS,
    WORK_FINISHED
} WORK_STATE;

typedef struct
{
    // Дескриптор сокета для обмена данными с клиентом.
    int client_sock_fd;
    //Нагрузка
    uint64_t load;
    // Текущее состояние протокола обмена данными с данным клиентом.
    WORK_STATE state;

} WORK_CONNECTION;


void info_manager_init(INFO_MANAGER *manager, char addr[], char port[], time_t seconds, int num_nodes) {
    struct addrinfo hints, *res, *p;
    int status;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if ((status = getaddrinfo(addr, port, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        exit(EXIT_FAILURE);
    }

    manager->listen_addr = *res->ai_addr;
    manager->max_time = seconds;
    manager->num_nodes = num_nodes;
    manager->is_init = true;
}

static void manager_init_socket(INFO_MANAGER* manager)
{
    if (manager->is_init == false) {
        fprintf(stderr, "[manager_init] Not init Info Manager!\n");
        exit(EXIT_FAILURE);
    }
    // Создаём сокет, слушающий подключения клиентов.
    manager->listen_sock_fd = socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK, 0);
    if (manager->listen_sock_fd == -1)
    {
        fprintf(stderr, "[manager_init] Unable to create socket!\n");
        exit(EXIT_FAILURE);
    }

    // Запрещаем перевод слушающего сокета в состояние TIME_WAIT.
    int setsockopt_yes = 1;
    if (setsockopt(manager->listen_sock_fd, SOL_SOCKET, SO_REUSEADDR, &setsockopt_yes, sizeof(setsockopt_yes)) == -1)
    {
        fprintf(stderr, "[manager_init] Unable to set SO_REUSEADDR socket option\n");
        exit(EXIT_FAILURE);
    }

    if (bind(manager->listen_sock_fd, (struct sockaddr*) &(manager->listen_addr), sizeof(manager->listen_addr)) == -1)
    {
        fprintf(stderr, "[manager_init] Unable to bind\n");
        fprintf(stderr, "%s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Активируем очередь запросов на подключение.
    if (listen(manager->listen_sock_fd, manager->num_nodes /* Размер очереди запросов на подключение */) == -1)
    {
        fprintf(stderr, "[manager_init] Unable to listen() on a socket\n");
        exit(EXIT_FAILURE);
    }
}
static void manager_close_listen_socket(INFO_MANAGER* manager) {

    if (close(manager->listen_sock_fd) == -1)
    {
        fprintf(stderr, "[manager_close_listen_socket] Unable to close() listen-socket\n");
        exit(EXIT_FAILURE);
    }
}

static void server_accept_connection_request(INFO_MANAGER* server, WORK_CONNECTION* conn)
{
    printf("Wait for worker_node to connect\n");

    // Создаём сокет для клиента из очереди на подключение.
    conn->client_sock_fd = accept(server->listen_sock_fd, NULL, NULL);
    if (conn->client_sock_fd == -1)
    {
        fprintf(stderr, "[server_accept_connection_request] Unable to accept() connection on a socket\n");
        exit(EXIT_FAILURE);
    }

    // Disable Nagle's algorithm:
    int setsockopt_arg = 1;
    if (setsockopt(conn->client_sock_fd, IPPROTO_TCP, TCP_NODELAY, &setsockopt_arg, sizeof(setsockopt_arg)) == -1)
    {
        fprintf(stderr, "[server_accept_connection_request] Unable to enable TCP_NODELAY socket option");
        exit(EXIT_FAILURE);
    }

    // Disable corking:
    setsockopt_arg = 0;
    if (setsockopt(conn->client_sock_fd, IPPROTO_TCP, TCP_CORK, &setsockopt_arg, sizeof(setsockopt_arg)) == -1)
    {
        fprintf(stderr, "[server_accept_connection_request] Unable to disable TCP_CORK socket option");
        exit(EXIT_FAILURE);
    }

    printf("Worker connected\n");
}

static void manager_get_worker_info(WORK_CONNECTION *work)
{

    struct node_info node;

    size_t bytes_read = recv(work->client_sock_fd, &node, sizeof(node), 0U);
    if (bytes_read != sizeof(node))
    {
        fprintf(stderr, "Unable to recv node info from worker\n");
        exit(EXIT_FAILURE);
    }
    work->load = node.max_worker_time * node.n_cores;
    DEBUG("Connect node with time: %ld and cores : %d",node.max_worker_time,node.n_cores);
}

static void manager_send_task(WORK_CONNECTION *work, struct worker_data send_data) {
    size_t bytes_written = write(work->client_sock_fd, &send_data, sizeof(send_data));
    if (bytes_written != sizeof(send_data))
    {
        fprintf(stderr, "Unable to send data block to client\n");
        exit(EXIT_FAILURE);
    }
    work->state = GET_ANS;
}
static double manager_get_worker_ans(WORK_CONNECTION *work) {
    double res;
    size_t bytes_read = recv(work->client_sock_fd, &res, sizeof(res), 0U);
    if (bytes_read != sizeof(res))
    {
        fprintf(stderr, "Unable to recv res from worker\n");
        exit(EXIT_FAILURE);
    }
    DEBUG("Return ans: %lf",res);
    return res;
}
void manager_close_worker_socket(WORK_CONNECTION *work) {
    if (close(work->client_sock_fd) == -1)
    {
        fprintf(stderr, "[manager_close_worker_socket] Unable to close() worker-socket\n");
        exit(EXIT_FAILURE);
    }
    work->state = WORK_FINISHED;
    work->client_sock_fd = -1;
}