#include "manager.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <address> <port> <max_time> <num_nodes>\n", argv[0]);
        return 1;
    }
    INFO_MANAGER info_manager;

    char *endptr = argv[3];
    time_t max_time = strtol(argv[3], &endptr, 10);
    if (*argv[3] == '\0' || *endptr != '\0')
    {
        fprintf(stderr, "Unable to parse time!\n");
        return 1;
    }

    endptr = argv[4];
    long num_workers = strtol(argv[4], &endptr, 10);
    if (*argv[4] == '\0' || *endptr != '\0')
    {
        fprintf(stderr, "Unable to parse number of workers!\n");
        return 1;
    }

    info_manager_init(&info_manager, argv[1], argv[2], max_time, num_workers);
    double res_value = 0;
    if (get_integral(&info_manager, 0, 1, 2, 0.01, &res_value)) {
        fprintf(stderr, "Error in get_integral\n");
        return 1;
    }
    printf("Result: %lf\n", res_value);

}