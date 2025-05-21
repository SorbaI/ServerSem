#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <pthread.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>

#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sched.h>

#include "common.h"
#include "worker.h"

//==================
// Управление сетью
//==================
static bool worker_connect_to_server(INFO_WORKER* worker)
{
    if (connect(worker->server_conn_fd, &worker->server_addr, sizeof(worker->server_addr)) == -1)
    {
        if (errno == ECONNREFUSED)
        {
            close(worker->server_conn_fd);

            return false;
        }

        fprintf(stderr, "[connect_to_master] Unable to connect() to master");
        exit(EXIT_FAILURE);
    }

    return true;
}

static void worker_close_socket(INFO_WORKER* worker)
{
    if (close(worker->server_conn_fd) == -1)
    {
        fprintf(stderr, "[worker_close_socket] Unable to close() worker socket\n");
        exit(EXIT_FAILURE);
    }
}


//=================================
// Передача данных по сети.
//=================================

static bool get_data(INFO_WORKER* worker)
{
    size_t bytes_read = recv(worker->server_conn_fd, &worker->data, sizeof(worker->data), MSG_WAITALL);
    if (bytes_read != sizeof(worker->data))
    {
        fprintf(stderr, "Unable to recv data from server\n");
        return false;
    }

    return true;
}

static bool send_result(INFO_WORKER *worker)
{
    if (!worker)
        return false;
    struct worker_result res_to_send = {.value = worker->result};

    size_t bytes_written = write(worker->server_conn_fd, &res_to_send, sizeof(res_to_send));
    if (bytes_written != sizeof(res_to_send))
    {
        fprintf(stderr, "Unable to send result to server\n");
        return false;
    }

    return true;
}

static bool send_node_info(INFO_WORKER *worker, struct node_info *info)
{
    if (!worker)
        return false;

    size_t bytes_written = write(worker->server_conn_fd, info, sizeof(*info));
    if (bytes_written != sizeof(*info))
    {
        fprintf(stderr, "Unable to send node info to server\n");
        return false;
    }

    return true;
}

//============================
// Вычисление значений функций
//============================
static double func_val(FUNC_TABLE func_id, double x)
{
    switch(func_id) {
        case EXP:
            return exp(x);
        case SIN:
            return sin(x);
        case SQR:
            return x * x;
        default:
            fprintf(stderr, "Unexpected id for function\n");
            exit(EXIT_FAILURE);
    }
}

//============================
// Распределение задач
//============================
struct thread_args
{
    int func_id;
    long long parts;
    double left;
    double step;
    double retval;
};

static void *thread_func(void *t_args)
{
    double result = 0;
    struct thread_args *args = (struct thread_args *) t_args;
    for (long long i = 0; i < args->parts; ++i) {
        result += args->step * func_val(args->func_id, args->left + args->step * i + args->step / 2);
    }
    args->retval = result;
    return NULL;
}

static double distributed_counting(INFO_WORKER *worker)
{
    // Проверка валидности запрашиваемого числа ядер
    if (worker->n_cores > get_nprocs()) {
        fprintf(stderr, "[distributed_counting] the number of processors currently \
                available in the system is less than required\n");
    }

    int threads_num = worker->n_cores;
    pthread_t threads[threads_num];
    struct thread_args args[threads_num];
    // Левая граница подотрезка для потока.
    double left = worker->data.left;
    // Число подотрезков для одного потока.
    long long thread_parts = worker->data.num_steps / threads_num;

    for (int i = 0; i < threads_num; ++i) {
        // Выбор ядра для выполнения потока.
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(i % worker->n_cores, &cpuset);
        
        pthread_attr_t thread_attr;
        if(pthread_attr_init(&thread_attr)) {
            fprintf(stderr, "pthread_attr_init returns with error\n");
            exit(EXIT_FAILURE);
        }

        // Устанавливаем аффинность потока.
        if (pthread_attr_setaffinity_np(&thread_attr, sizeof(cpuset), &cpuset)) {
            fprintf(stderr, "pthread_attr_setaffinity_np returns with error\n");
            exit(EXIT_FAILURE);
        }

        args[i].func_id = worker->data.func_id;
        args[i].left    = left;
        args[i].step    = worker->data.step;
        args[i].parts   = thread_parts;
        if (i < worker->data.num_steps % threads_num)
            ++args[i].parts;
         
        if (pthread_create(&threads[i], &thread_attr, thread_func, &args[i])) {
            fprintf(stderr, "Unable to create thread\n");
            exit(EXIT_FAILURE);
        }

        // Удаляем объект аттрибутов потока.
        if (pthread_attr_destroy(&thread_attr)) {
            fprintf(stderr, "Unable to destroy a thread attributes object\n");
            exit(EXIT_FAILURE);
        }

    }

    double result = 0;
    // Ждём завершения потоков и вычисляем результат.
    for (int i = 0; i < threads_num; ++i)
    {
        if (pthread_join(threads[i], NULL)) {
            fprintf(stderr, "Unable to join a thread\n");
            exit(EXIT_FAILURE);
        }
        result += args[i].retval;
    }
    return result;
}

//============================
// Интерфейс исполнителя
//============================

INFO_WORKER init_worker(int n_cores, time_t max_time, char *node, char *service)
{
    INFO_WORKER worker = {.n_cores = n_cores, .max_time = max_time};

    worker.server_conn_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (worker.server_conn_fd == -1)
    {
        fprintf(stderr, "[init_worker] Unable to create socket()\n");
        exit(EXIT_FAILURE);
    }

    // Формируем желаемый адрес для подключения.
    struct addrinfo hints;

    struct addrinfo* res;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = 0;
    hints.ai_protocol = 0;

    if (getaddrinfo(node, service, &hints, &res) != 0)
    {
        fprintf(stderr, "[init_worker] Unable to call getaddrinfo()\n");
        exit(EXIT_FAILURE);
    }

    // Проверяем, что для данного запроса существует только один адрес.
    if (res->ai_next != NULL)
    {
        fprintf(stderr, "[init_worker] Ambigous result of getaddrinfo()\n");
        exit(EXIT_FAILURE);
    }

    worker.server_addr = *res->ai_addr;
    
    return worker;
}

void connect_to_server(INFO_WORKER *worker) {
    // Подключение к серверу.
    bool connected_to_server = worker_connect_to_server(worker);
    while (!connected_to_server)
    {
        // Ожидаем, пока сервер проснётся.
        sleep(1U);

        printf("Wait for server to start\n");

        connected_to_server = worker_connect_to_server(worker);
    }

    // Отправка данных об узле.
    struct node_info info = {.n_cores = worker->n_cores, .max_worker_time = worker->max_time};
    bool success = send_node_info(worker, &info);
    if (!success)
    {
        worker_close_socket(worker);
        exit(EXIT_FAILURE);
    }

    // Получение данных.
    success = get_data(worker);
    if (!success)
    {
        worker_close_socket(worker);
        exit(EXIT_FAILURE);
    }

    // Вычисление результата.
    worker->result = distributed_counting(worker);

    // Отправка результата.
    success = send_result(worker);
    if (!success)
    {
        worker_close_socket(worker);
        exit(EXIT_FAILURE);
    }
}

void worker_close(INFO_WORKER *worker)
{
    // Освобождение сокета.
    if (worker->server_conn_fd >= 0)
        worker_close_socket(worker);
}

//============================
// Основная процедура исполнителя.
//============================

// node = "127.0.0.1"
// service = "1337"

int main(int argc, char** argv)
{
    time_t max_time = 10;
    int n_cores = 1;
    if (argc != 3)
    {
        fprintf(stderr, "Usage: worker <node> <service>\n");
        exit(EXIT_FAILURE);
    }

    // Данные исполнителя.
    INFO_WORKER worker = init_worker(n_cores, max_time, argv[1], argv[2]);

    connect_to_server(&worker);

    worker_close(&worker);

    printf("Sent answer\n");

    return EXIT_SUCCESS;
}
