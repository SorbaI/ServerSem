//================
// Данные исполнителя.
//================

// Максимальное допустимое время вычисления.
int MAX_TIME;

// Максимальное количество ядер, задействованных для вычисления на данном рабочем узле.
int N_CORES;

typedef struct
{
    // Дескриптор сокета для подключения к серверу.
    int server_conn_fd;

    // Адрес для подключению к серверу.
    struct sockaddr server_addr;

    // Максимальное время вычисления.
    time_t max_time;

    // Количество ядер.
    int n_cores;

    // Данные для вычисления интеграла.
    struct worker_data data;
    
    // Результат вычислений.
    double result;
} INFO_WORKER;


//================
// Интерфейс исполнителя.
//================

// Инициализация структуры исполнителя.
INFO_WORKER init_worker(int n_cores, time_t max_time, char *node, char *service);

// Работа с сервером.
void connect_to_server(INFO_WORKER *worker);

// Закрытие открытого сокета.
void worker_close(INFO_WORKER *worker);
