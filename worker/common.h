typedef enum
{
    EXP,
    SIN,
    SQR,
    NOT_SUPPORT,
} FUNC_TABLE;

struct worker_data {
    FUNC_TABLE func_id; 
    double left;
    double step;
    long long num_steps;
};

struct worker_result {
    double value;
};

struct node_info {
    time_t max_worker_time;
    int n_cores;
};
