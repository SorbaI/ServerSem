#include "manager-common.h"
#include <memory.h>
#include <poll.h>
#include <math.h>
#include <sched.h>
#include <pthread.h>


static void poll_server_wait_for_worker(struct pollfd* pollfds, INFO_MANAGER* server)
{
    struct pollfd* pollfd = &pollfds[0U];

    pollfd->fd      = server->listen_sock_fd;
    pollfd->events  = POLLIN;
    pollfd->revents = 0U;
}

static void poll_server_do_not_wait_for_workers(struct pollfd* pollfds)
{
    struct pollfd* pollfd = &pollfds[0U];

    pollfd->fd      = -1;
    pollfd->events  = 0U;
    pollfd->revents = 0U;
}

static void poll_manager_wait_for_answer(struct pollfd* pollfds, size_t conn_i, WORK_CONNECTION *work) {
    struct pollfd* pollfd = &pollfds[1 + conn_i];

    pollfd->fd      = work->client_sock_fd;
    pollfd->events  = POLLIN | POLLHUP;
    pollfd->revents = 0U;
}

static void poll_server_do_not_wait_for_ans(struct pollfd* pollfds, size_t conn_i){
    struct pollfd* pollfd = &pollfds[conn_i + 1];

    pollfd->fd      = -1;
    pollfd->events  = 0U;
    pollfd->revents = 0U;
}

static void poll_manager_wait_work_info(struct pollfd* pollfds, size_t conn_i, WORK_CONNECTION *work) {
    struct pollfd* pollfd = &pollfds[1 + conn_i];

    pollfd->fd      = work->client_sock_fd;
    pollfd->events  = POLLIN|POLLHUP;
    pollfd->revents = 0U;
}


static double get_step(FUNC_TABLE func_id, double left, double right, double precision) {
    double max_derivative_2 = get_max_derivate_2(func_id,left,right);
    if (max_derivative_2 == 0) {
        return 1;
    }
    return cbrt(24 * precision / max_derivative_2);
}

int get_integral(INFO_MANAGER *manager, FUNC_TABLE func_id, double left, double right, double precision, double *res_value) {
    if (func_id >= NOT_SUPPORT || func_id < 0) {
        return -EFUNCID;
    }
    if (left > right || res_value == NULL) {
        return -EVALUE;
    }
    if (right == left) {
        *res_value = 0;
        return 0;
    }
    double step = get_step(func_id, left, right, precision);
    uint64_t num_count = (uint64_t)(ceil(fabs(right - left) / step)) + 2;
    // Избавляемся от неполных шагов
    step = (right - left) / num_count;


    WORK_CONNECTION* works = calloc(manager->num_nodes, sizeof(WORK_CONNECTION));
    if (works == NULL)
    {
        fprintf(stderr, "Unable to allocate connection states\n");
        exit(EXIT_FAILURE);
    }

    for (size_t conn_i = 0U; conn_i < manager->num_nodes; conn_i++)
    {
        works[conn_i].state  = CONNECTION_EMPTY;
    }

    // Аллоцируем массив файловых дескрипторов для мониторинга.
    struct pollfd* pollfds = calloc(manager->num_nodes + 1U, sizeof(struct pollfd));
    if (pollfds == NULL)
    {
        fprintf(stderr, "Unable to allocate poll file descriptor array\n");
        exit(EXIT_FAILURE);
    }

    size_t num_connected_workers = 0U;
    size_t num_init_workers = 0U;
    uint64_t value_load = 0;

    manager_init_socket(manager);


    // Ожидаем подключения всех Рабочих узлов и собираем информацию о них.
    while (num_init_workers != manager->num_nodes)
    {
        if (num_connected_workers != manager->num_nodes) {
            poll_server_wait_for_worker(pollfds, manager);
        } else {
            poll_server_do_not_wait_for_workers(pollfds);
        }

        int pollret = poll(pollfds, 1U + num_connected_workers, 100000);
        if (pollret == -1)
        {
            fprintf(stderr, "Unable to poll-wait for data on descriptors!\n");
            exit(EXIT_FAILURE);
        }

        if (pollfds[0U].revents & POLLIN)
        {
            server_accept_connection_request(manager, &works[num_connected_workers]);

            works[num_connected_workers].state = GET_INFO;
            poll_manager_wait_work_info(pollfds, num_connected_workers, &works[num_connected_workers]);

            num_connected_workers += 1U;
        } 

        for (size_t conn_i = 0U; conn_i < num_connected_workers; ++conn_i)
        {
            if (pollfds[1U + conn_i].revents & POLLHUP)
            {  
                fprintf(stderr, "Unexpected POLLHUP\n");
                exit(EXIT_FAILURE);
            }

            if (pollfds[1U + conn_i].revents & POLLIN)
            {
                switch (works[conn_i].state)
                {
                case CONNECTION_EMPTY:
                case WORK_FINISHED:
                case GET_ANS:
                    fprintf(stderr, "Unexpected state!\n");
                    exit(EXIT_FAILURE);
                case GET_INFO:
                    manager_get_worker_info(&works[conn_i]);
                    works[conn_i].state = SEND_TASK;
                    value_load += works[conn_i].load;
                    num_init_workers++;
                    break;
                case SEND_TASK:
                }
            }
        }
    }
    manager_close_listen_socket(manager);
    if (value_load == 0) {
        fprintf(stderr, "Error workers haven't resourses\n");
        exit(EXIT_FAILURE);
    }
    time_t start_time = time(NULL);
    uint64_t num_count_was = 0;
    struct worker_data data;
    data.func_id = func_id;
    for (size_t conn_i = 0; conn_i < manager->num_nodes - 1; ++conn_i) {
        double part_load = (double)works[conn_i].load / value_load;
        uint64_t num_steps_i = num_count * (part_load);
        data.num_steps = num_steps_i;
        data.left = left + step * num_count_was;
        num_count_was += num_steps_i;
        data.step = step;
        manager_send_task(&works[conn_i], data);
        poll_manager_wait_for_answer(pollfds,conn_i,&works[conn_i]);
    }
    // last worker proccess separate
    data.num_steps = num_count - num_count_was;
    data.left = left + step * num_count_was;
    data.step = step;
    double ans = 0;
    manager_send_task(&works[manager->num_nodes - 1],data);
    poll_manager_wait_for_answer(pollfds,manager->num_nodes - 1,&works[manager->num_nodes - 1]);
    while (num_connected_workers != 0) {
        time_t wait_time = manager->max_time - (time(NULL) - start_time);
        if(wait_time < 0) {
            fprintf(stderr, "Time ended!\n");
            exit(EXIT_FAILURE);
        }
        int pollret = poll(pollfds, 1U + num_connected_workers, wait_time);
        if (pollret == -1)
        {
            fprintf(stderr, "Unable to poll-wait for data on descriptors!\n");
            exit(EXIT_FAILURE);
        }
        for (size_t conn_i = 0U; conn_i < num_connected_workers; ++conn_i)
        {
            if (pollfds[1U + conn_i].revents & POLLHUP)
            {  
                fprintf(stderr, "Unexpected POLLHUP\n");
                exit(EXIT_FAILURE);
            }

            if (pollfds[1U + conn_i].revents & POLLIN)
            {
                switch (works[conn_i].state)
                {
                case CONNECTION_EMPTY:
                case GET_INFO:
                case SEND_TASK:
                    fprintf(stderr, "Unexpected state!\n");
                    exit(EXIT_FAILURE);
                case GET_ANS:
                    ans += manager_get_worker_ans(&works[conn_i]);
                    works[conn_i].state = WORK_FINISHED;
                    num_connected_workers--;
                    poll_server_do_not_wait_for_ans(pollfds, conn_i);
                    manager_close_worker_socket(&works[conn_i]);
                    break;
                case WORK_FINISHED:
                }
            }
        }
    }
    *res_value = ans;
    return 0;
}
