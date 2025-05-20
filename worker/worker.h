// Максимальное допустимое время вычисления.
int MAX_TIME;

// Максимальное количество ядер, задействованных для вычисления на данном рабочем узле.
int N_CORES;

// Инициализация структуры исполнителя.
INFO_WORKER init_worker(int n_cores, time_t max_time, char *node, char *service);

// Работа с сервером.
void connect_to_server(INFO_WORKER *worker);

// Закрытие открытого сокета.
void worker_close(&worker);
