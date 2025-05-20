enum FUNC_TABLE = 
{
    EXP,
    SIN,
    SQR,
    NOT_SUPPORT,
};

struct worker_data {
    FUNC_TABLE func_id; 
    double left;
    double right;
    double step;
    long long num_steps;
};

struct worker_result {
    int status;
    double value;
};

struct node_info {
    time_t max_worker_time;
    int n_cores;
};
