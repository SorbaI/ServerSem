#define DEBUG(...) printf(__VA_ARGS__);

#include <stdbool.h>
#include <time.h>
#include <arpa/inet.h>

typedef struct
{
    //Адрес для прослушивания запросов на подключение.
    struct sockaddr listen_addr;
    // Максимальное время работы в секундах
    time_t max_time;
    // Количество рабочих узлов, необходимых для запуска вычисления
    size_t num_nodes;
    // Дескриптор слушающего сокета для первоначального подключения клиентов.
    int listen_sock_fd;
    bool is_init;
} INFO_MANAGER;

typedef enum
{
	EXP,
	SIN,
	SQR,
	NOT_SUPPORT,
} FUNC_TABLE;

void info_manager_init(INFO_MANAGER *manager, char addr[], char port[], time_t seconds, int num_nodes);
int get_integral(INFO_MANAGER *manager, FUNC_TABLE func_id, double left, double right, double precision, double *res_value);