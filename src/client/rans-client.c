// fanout will be treated as duplicates in rans-client - 11 July 2016, Musa
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <pthread.h>
#include <signal.h>

#include "../common/common.h"
#include "../common/cdf.h"
#include "../common/conn.h"


/* the structure of a flow request */
struct flow_request
{
    struct conn_node *node;
    struct flow_metadata metadata;
};

bool verbose_mode = false;  /* by default, we don't give more detailed output */

char config_file_name[80] = {0};    /* configuration file name */
char dist_file_name[80] = {0};  /* size distribution file name */
char log_prefix[] = "log";  /* default */
char fct_log_suffix[] = "flows.txt";
char rct_log_suffix[] = "reqs.txt";
char rct_log_name[80] = {0};    /* request completion times (RCT) log file name */
char fct_log_name[80] = {0};    /* request flow completion times (FCT) log file name */
char result_script_name[80] = {0};  /* name of script file to parse final results */
int seed = 0;   /* random seed */
unsigned int usleep_overhead_us = 0;    /* usleep overhead */
struct timeval tv_start, tv_end;    /* start and end time of traffic */

/* request array to store requests */
struct request *requests = NULL; /* array to store all the requests */

/* per-server variables */
unsigned int num_server = 0;    /* total number of servers */
unsigned int *server_port = NULL;   /* ports of servers */
char (*server_addr)[20] = NULL; /* IP addresses of servers */
unsigned int *server_flow_count = NULL; /* the number of flows generated by each server */

unsigned int num_fanout = 0;    /* number of fanouts */
unsigned int *fanout_size = NULL;
unsigned int *fanout_prob = NULL;
unsigned int fanout_prob_total = 0;
unsigned int max_fanout_size = 1;

unsigned int num_dscp = 0;  /* number of DSCP */ /*Todo: not being used, cleanup*/
unsigned int *dscp_value = NULL;
unsigned int *dscp_prob = NULL;
unsigned int dscp_prob_total = 0;

unsigned int num_rate = 0;  /* number of sending rates */
unsigned int *rate_value = NULL;
unsigned int *rate_prob = NULL;
unsigned int rate_prob_total = 0;

double load = -1;   /* network load (mbps) */
unsigned int req_total_num = 0; /* total number of requests to generate */
unsigned int flow_total_num = 0;    /* total number of flows */
unsigned int req_total_time = 0;    /* total time to generate requests */
struct cdf_table *req_size_dist = NULL;
unsigned int period_us;  /* average request arrival interval (us) */

/* per-flow variables */
unsigned int *flow_req_id = NULL;   /* request ID of the flow */
struct timeval *flow_start_time = NULL; /* start time of flow */
struct timeval *flow_stop_time = NULL;  /* stop time of flow */

struct conn_list *connection_lists = NULL;  /* connection pool */
unsigned int global_flow_id = 0;

/* print usage of the program */
void print_usage(char *program);
/* read command line arguments */
void read_args(int argc, char *argv[]);
/* read configuration file */
void read_config(char *file_name);
/* set request variables */
void set_req_variables();
/* receive traffic from established connections */
void *listen_connection(void *ptr);
/* generate rans requests */
void run_rans_requests();
/* generate a rans request to some servers */
void run_rans_request(unsigned int req_id);
/* generate a flow request to a server */
void *run_flow(void *ptr);
/* terminate all existing connections */
void exit_connections();
/* terminate a connection */
void exit_connection(struct conn_node *node);
/* print statistic data */
void print_statistic();
/* clean up resources */
void cleanup();

int main(int argc, char *argv[])
{
    unsigned int i = 0;
    struct conn_node *ptr = NULL;

    /* read program arguments */
    read_args(argc, argv);

    /* set seed value for random number generation */
    if (seed == 0)
    {
        gettimeofday(&tv_start, NULL);
        srand((tv_start.tv_sec*1000000) + tv_start.tv_usec);
    }
    else
        srand(seed);

    /* read configuration file */
    read_config(config_file_name);
    /* set request variables */
    set_req_variables();

    /* calculate usleep overhead */
    usleep_overhead_us = get_usleep_overhead(20);
    if (verbose_mode)
    {
        printf("===========================================\n");
        printf("The usleep overhead is %u us\n", usleep_overhead_us);
        printf("===========================================\n");
    }

    /* we use calloc here to implicitly initialize struct conn_list as 0 */
    connection_lists = (struct conn_list*)calloc(num_server, sizeof(struct conn_list));
    if (!connection_lists)
    {
        cleanup();
        error("Error: calloc connection_lists");
    }

    /* initialize connection pool and establish connections to servers */
    for (i = 0; i < num_server; i++)
    {
        if (!init_conn_list(&connection_lists[i], i, server_addr[i], server_port[i]))
        {
            cleanup();
            error("Error: init_conn_list");
        }
        if (!insert_conn_list(&connection_lists[i], max(max_fanout_size, TG_PAIR_INIT_CONN)))
        {
            cleanup();
            error("Error: insert_conn_list");
        }
        if (verbose_mode)
            print_conn_list(&connection_lists[i]);
    }

    /* start threads to receive traffic */
    for (i = 0; i < num_server; i++)
    {
        ptr = connection_lists[i].head;
        while (true)
        {
            if (!ptr)
                break;
            else
            {
                pthread_create(&(ptr->thread), NULL, listen_connection, (void*)ptr);
                ptr = ptr->next;
            }
        }
    }

    printf("===========================================\n");
    printf("Start to generate requests\n");
    printf("===========================================\n");
    gettimeofday(&tv_start, NULL);
    global_flow_id =  0;
    run_rans_requests();

    /* close existing connections */
    printf("===========================================\n");
    printf("Exit connections\n");
    printf("===========================================\n");
    exit_connections();
    gettimeofday(&tv_end, NULL);

    printf("===========================================\n");
    for (i = 0; i < num_server; i++)
        print_conn_list(&connection_lists[i]);
    printf("===========================================\n");
    print_statistic();

    /* release resources */
    cleanup();

    /* parse results */
    if (strlen(result_script_name) > 0)
    {
        char cmd[180] = {0};
        printf("===========================================\n");
        printf("Flow completion times (FCT) results\n");
        printf("===========================================\n");
        sprintf(cmd, "python %s %s", result_script_name, fct_log_name);
        system(cmd);

        memset (cmd, 0, 180);
        printf("===========================================\n");
        printf("Request completion times (RCT) results\n");
        printf("===========================================\n");
        sprintf(cmd, "python %s %s", result_script_name, rct_log_name);
        system(cmd);
    }

    return 0;
}

/* print usage of the program */
void print_usage(char *program)
{
    printf("Usage: %s [options]\n", program);
    printf("-b <bandwidth>  expected average RX bandwidth in Mbits/sec\n");
    printf("-c <file>       configuration file (required)\n");
    printf("-n <number>     number of requests (instead of -t)\n");
    printf("-t <time>       time in seconds (instead of -n)\n");
    printf("-l <prefix>     log file name prefix (default %s)\n", log_prefix);
    printf("-s <seed>       seed to generate random numbers (default current time)\n");
    printf("-r <file>       python script to parse result files\n");
    printf("-v              give more detailed output (verbose)\n");
    printf("-h              display help information\n");
}

/* read command line arguments */
void read_args(int argc, char *argv[])
{
    unsigned int i = 1;
    bool error = false;

    if (argc == 1)
    {
        print_usage(argv[0]);
        exit(EXIT_SUCCESS);
    }

    sprintf(fct_log_name, "%s_%s", log_prefix, fct_log_suffix);
    sprintf(rct_log_name, "%s_%s", log_prefix, rct_log_suffix);

    while (i < argc)
    {
        if (strlen(argv[i]) == 2 && strcmp(argv[i], "-b") == 0)
        {
            if (i+1 < argc)
            {
                load = atof(argv[i+1]);
                if (load <= 0)
                {
                    printf("Invalid average RX bandwidth: %f\n", load);
                    print_usage(argv[0]);
                    exit(EXIT_FAILURE);
                }
                i += 2;
            }
            else
            {
                printf("Cannot read average RX bandwidth\n");
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
            }
        }
        else if (strlen(argv[i]) == 2 && strcmp(argv[i], "-c") == 0)
        {
            if (i+1 < argc && strlen(argv[i+1]) < sizeof(config_file_name))
            {
                sprintf(config_file_name, "%s", argv[i+1]);
                i += 2;
            }
            else
            {
                printf("Cannot read configuration file name\n");
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
            }
        }
        else if (strlen(argv[i]) == 2 && strcmp(argv[i], "-n") == 0)
        {
            if (i+1 < argc)
            {
                req_total_num = (unsigned int)strtoul(argv[i+1], NULL, 10);
                i += 2;
            }
            else
            {
                printf("Cannot read number of requests\n");
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
            }
        }
        else if (strlen(argv[i]) == 2 && strcmp(argv[i], "-t") == 0)
        {
            if (i+1 < argc)
            {
                req_total_time = (unsigned int)strtoul(argv[i+1], NULL, 10);
                i += 2;
            }
            else
            {
                printf("Cannot read time to generate requests\n");
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
            }
        }
        else if (strlen(argv[i]) == 2 && strcmp(argv[i], "-l") == 0)
        {
            if (i+1 < argc && strlen(argv[i+1]) + 1 + strlen(fct_log_suffix) < sizeof(fct_log_name) && strlen(argv[i+1]) + 1 + strlen(rct_log_suffix) < sizeof(rct_log_name))
            {
                sprintf(fct_log_name, "%s_%s", argv[i+1], fct_log_suffix);
                sprintf(rct_log_name, "%s_%s", argv[i+1], rct_log_suffix);
                i += 2;
            }
            else
            {
                printf("Cannot read log file prefix\n");
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
            }
        }
        else if (strlen(argv[i]) == 2 && strcmp(argv[i], "-s") == 0)
        {
            if (i+1 < argc)
            {
                seed = atoi(argv[i+1]);
                i += 2;
            }
            else
            {
                printf("Cannot read seed value\n");
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
            }
        }
        else if (strlen(argv[i]) == 2 && strcmp(argv[i], "-r") == 0)
        {
            if (i+1 < argc && strlen(argv[i+1]) < sizeof(result_script_name))
            {
                sprintf(result_script_name, "%s", argv[i+1]);
                i += 2;
            }
            else
            {
                printf("Cannot read script file name\n");
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
            }
        }
        else if (strlen(argv[i]) == 2 && strcmp(argv[i], "-v") == 0)
        {
            verbose_mode = true;
            i++;
        }
        else if (strlen(argv[i]) == 2 && strcmp(argv[i], "-h") == 0)
        {
            print_usage(argv[0]);
            exit(EXIT_SUCCESS);
        }
        else
        {
            printf("Invalid option %s\n", argv[i]);
            print_usage(argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    if (load < 0)
    {
        printf("You need to specify the average RX bandwidth (-b)\n");
        error = true;
    }

    if (req_total_num == 0 && req_total_time == 0)
    {
        printf("You need to specify either the number of requests (-n) or the time to generate requests (-t)\n");
        error = true;
    }
    else if (req_total_num > 0 && req_total_time > 0)
    {
        printf("You cannot specify both the number of requests (-n) and the time to generate requests (-t)\n");
        error = true;
    }

    if (error)
    {
        print_usage(argv[0]);
        exit(EXIT_FAILURE);
    }
}

/* read configuration file */
void read_config(char *file_name)
{
    FILE *fd = NULL;
    char line[256] = {0};
    char key[80] = {0};
    num_server = 0; /* number of senders */
    unsigned int num_dist = 0;  /* number of flow size distributions */
    num_fanout = 0; /* number of fanouts (optinal) */
    num_dscp = 0;   /* number of DSCP (optional) */
    num_rate = 0;   /* number of sending rates (optional) */

    printf("===========================================\n");
    printf("Reading configuration file %s\n", file_name);
    printf("===========================================\n");

    /* parse configuration file for the first time */
    fd = fopen(file_name, "r");
    if (!fd)
        error("Error: open configuration file for the first time");

    while (fgets(line, sizeof(line), fd))
    {
        sscanf(line, "%s", key);
        if (!strcmp(key, "server"))
            num_server++;
        else if (!strcmp(key, "req_size_dist"))
            num_dist++;
        else if (!strcmp(key, "fanout"))
            num_fanout++;
        else if (!strcmp(key, "dscp"))
            num_dscp++;
        else if (!strcmp(key, "rate"))
            num_rate++;
    }

    fclose(fd);

    if (num_server < 1)
        error("Error: configuration file should provide at least one server");
    if (num_dist != 1)
        error("Error: configuration file should provide exactly one request size distribution");

    /* initialize configuration */
    /* per-server variables*/
    server_port = (unsigned int*)calloc(num_server, sizeof(unsigned int));
    server_addr = (char (*)[20])calloc(num_server, sizeof(char[20]));
    server_flow_count = (unsigned int*)calloc(num_server, sizeof(unsigned int));
    /* fanout size and probability */
    fanout_size = (unsigned int*)calloc(max(num_fanout, 1), sizeof(unsigned int));
    fanout_prob = (unsigned int*)calloc(max(num_fanout, 1), sizeof(unsigned int));
    /* DSCP and probability */
    dscp_value = (unsigned int*)calloc(max(num_dscp, 1), sizeof(unsigned int));
    dscp_prob = (unsigned int*)calloc(max(num_dscp, 1), sizeof(unsigned int));
    /* sending rate value and probability */
    rate_value = (unsigned int*)calloc(max(num_rate, 1), sizeof(unsigned int));
    rate_prob = (unsigned int*)calloc(max(num_rate, 1), sizeof(unsigned int));

    if (!server_port || !server_addr || !server_flow_count || !fanout_size || !fanout_prob || !dscp_value || !dscp_prob || !rate_value || !rate_prob)
    {
        cleanup();
        error("Error: calloc per-server variables");
    }

    /* second time */
    num_server = 0;
    num_fanout = 0;
    num_dscp = 0;
    num_rate = 0;

    fd = fopen(file_name, "r");
    if (!fd)
    {
        cleanup();
        error("Error: open the configuration file for the second time");
    }

    while (fgets(line, sizeof(line), fd))
    {
        remove_newline(line);
        sscanf(line, "%s", key);

        if (!strcmp(key, "server"))
        {
            sscanf(line, "%s %s %u", key, server_addr[num_server], &server_port[num_server]);
            if (verbose_mode)
                printf("Server[%u]: %s, Port: %u\n", num_server, server_addr[num_server], server_port[num_server]);
            num_server++;
        }
        else if (!strcmp(key, "req_size_dist"))
        {
            sscanf(line, "%s %s", key, dist_file_name);
            if (verbose_mode)
                printf("Loading request size distribution: %s\n", dist_file_name);

            req_size_dist = (struct cdf_table*)malloc(sizeof(struct cdf_table));
            if (!req_size_dist)
            {
                cleanup();
                error("Error: malloc req_size_dist");
            }

            init_cdf(req_size_dist);
            load_cdf(req_size_dist, dist_file_name);
            if (verbose_mode)
            {
                printf("===========================================\n");
                print_cdf(req_size_dist);
                printf("Average request size: %.2f bytes\n", avg_cdf(req_size_dist));
                printf("===========================================\n");
            }
        }
        else if (!strcmp(key, "dscp"))
        {
            sscanf(line, "%s %u %u", key, &dscp_value[num_dscp], &dscp_prob[num_dscp]);
            if (dscp_value[num_dscp] < 0 || dscp_value[num_dscp] >= 64)
            {
                cleanup();
                error("Invalid DSCP value");
            }
            else if (dscp_prob[num_dscp] < 0)
            {
                cleanup();
                error("Invalid DSCP probability value");
            }
            dscp_prob_total += dscp_prob[num_dscp];
            if (verbose_mode)
                printf("DSCP: %u, Prob: %u\n", dscp_value[num_dscp], dscp_prob[num_dscp]);
            num_dscp++;
        }
        else if (!strcmp(key, "rate"))
        {
            sscanf(line, "%s %uMbps %u", key, &rate_value[num_rate], &rate_prob[num_rate]);
            if (rate_value[num_rate] < 0)
            {
                cleanup();
                error("Invalid sending rate value");
            }
            else if (rate_prob[num_rate] < 0)
            {
                cleanup();
                error("Invalid sending rate probability value");
            }
            rate_prob_total += rate_prob[num_rate];
            if (verbose_mode)
                printf("Rate: %uMbps, Prob: %u\n", rate_value[num_rate], rate_prob[num_rate]);
            num_rate++;
        }
        else if (!strcmp(key, "fanout"))
        {
            sscanf(line, "%s %u %u", key, &fanout_size[num_fanout], &fanout_prob[num_fanout]);
            if (fanout_size[num_fanout] < 1)
            {
                cleanup();
                error("Invalid fanout size");
            }
            else if(fanout_prob[num_fanout] < 0)
            {
                cleanup();
                error("Invalid fanout probability value");
            }

            fanout_prob_total += fanout_prob[num_fanout];
            if (fanout_size[num_fanout] > max_fanout_size)
                max_fanout_size = fanout_size[num_fanout];

            if (verbose_mode)
                printf("Fanout: %u, Prob: %u\n", fanout_size[num_fanout], fanout_prob[num_fanout]);
            num_fanout++;
        }
    }

    fclose(fd);

    /* by default, fanout size is 1 */
    if (num_fanout == 0)
    {
        num_fanout = 1;
        fanout_size[0] = 1;
        fanout_prob[0] = 100;
        fanout_prob_total = fanout_prob[0];
        if (verbose_mode)
            printf("Fanout: %u, Prob: %u\n", fanout_size[0], fanout_prob[0]);
    }

    if (verbose_mode)
        printf("Max Fanout: %u\n", max_fanout_size);

    /* by default, DSCP value is 0 */
    if (num_dscp == 0)
    {
        num_dscp = 1;
        dscp_value[0] = 0;
        dscp_prob[0] = 100;
        dscp_prob_total = dscp_prob[0];
        if (verbose_mode)
            printf("DSCP: %u, Prob: %u\n", dscp_value[0], dscp_prob[0]);
    }

    /* by default, no rate limiting */
    if (num_rate == 0)
    {
        num_rate = 1;
        rate_value[0] = 0;
        rate_prob[0] = 100;
        rate_prob_total = rate_prob[0];
        if (verbose_mode)
            printf("Rate: %uMbps, Prob: %u\n", rate_value[0], rate_prob[0]);
    }
}

/* set request variables */
void set_req_variables()
{
    unsigned int i, k, server_id, flow_id = 0;
    unsigned long req_size_total = 0;
    double req_dscp_total = 0;
    unsigned long req_rate_total = 0;
    unsigned long req_interval_total = 0;

    /* calculate average request arrival interval */
    if (load > 0)
    {
        period_us = avg_cdf(req_size_dist) * 8 / load / TG_GOODPUT_RATIO;
        if (period_us <= 0)
        {
            cleanup();
            error("Error: period_us is not positive");
        }
    }
    else
    {
        cleanup();
        error("Error: load is not positive");
    }

    /* transfer time to the number of requests */
    if (req_total_num == 0 && req_total_time > 0)
        req_total_num = max((unsigned long)req_total_time * 1000000 / period_us, 1);

    /*per-request variables */
    requests = (struct request*)calloc(req_total_num, sizeof(struct request));

    if (!requests)
    {
        cleanup();
        error("Error: calloc per-request variables");
    }

    /* per request */
    for (i = 0; i < req_total_num; i++)
    {
        requests[i].server_priorities = (unsigned int*)calloc(num_server, sizeof(unsigned int));   /* initialize as 0 */
        if (!requests[i].server_priorities)
        {
            cleanup();
            error("Error: calloc per-request variables");
        }

        requests[i].id = i;
        requests[i].size = gen_random_cdf(req_size_dist);    /* request size */
        requests[i].duplicates = gen_value_weight(fanout_size, fanout_prob, num_fanout, fanout_prob_total);  /* request fanout */
        requests[i].dscp = gen_value_weight(dscp_value, dscp_prob, num_dscp, dscp_prob_total);    /* request DSCP */
        requests[i].rate = gen_value_weight(rate_value, rate_prob, num_rate, rate_prob_total);   /* sending rate */
        requests[i].sleep_us = poission_gen_interval(1.0/period_us); /* sleep interval based on poission process */

        req_size_total += requests[i].size;
        req_dscp_total += requests[i].dscp;
        req_rate_total += requests[i].rate;
        req_interval_total += requests[i].sleep_us;
        flow_total_num += requests[i].duplicates;

        
        /* each flow in this request */
        //no of unique servers should be more than the flow fanout.
        if (num_server<requests[i].duplicates)
        {
            cleanup();
            error("Error: fanout larger than the number of servers");
        }
        bool *servers_selected;
        servers_selected = (bool*)calloc(num_server, sizeof(bool));
        for (k = 0; k < requests[i].duplicates; k++)
        {
            server_id = rand() % num_server;
            while (servers_selected[server_id]!=0) // chose unique servers for each duplicate (= each fanout flow)
            {
                server_id = rand() % num_server;
            }
            servers_selected[server_id]=1;

            requests[i].server_priorities[server_id]= (k+1);
            server_flow_count[server_id]++;
            if (verbose_mode && k==0)
            {
                printf("Primary server id is %i for request %i\n", server_id, i);
            }
        }
        free(servers_selected);
    }

    /* per-flow variables */
    flow_req_id = (unsigned int*)calloc(flow_total_num, sizeof(unsigned int));
    flow_start_time = (struct timeval*)calloc(flow_total_num, sizeof(struct timeval));
    flow_stop_time = (struct timeval*)calloc(flow_total_num, sizeof(struct timeval));

    if (!flow_req_id || !flow_start_time || !flow_stop_time)
    {
        cleanup();
        error("Error: calloc per-flow variables");
    }

    /* assign request ID to each flow */
    flow_id = 0;
    for (i = 0; i < req_total_num; i++)
        for (k = 0; k < requests[i].duplicates; k++)
            flow_req_id[flow_id++] = i;

    if (flow_id != flow_total_num)
        perror("Not all the flows have request ID");

    printf("===========================================\n");
    printf("We generate %u requests (%u flows) in total\n", req_total_num, flow_total_num);

    for (i = 0; i < num_server; i++)
        printf("%s:%u    %u flows\n", server_addr[i], server_port[i], server_flow_count[i]);

    printf("===========================================\n");
    printf("The average request arrival interval is %lu us\n", req_interval_total/req_total_num);
    printf("The average request size is %lu bytes\n", req_size_total/req_total_num);
    // printf("The average flow size is %lu bytes\n", req_size_total/flow_total_num);
    printf("The average request fanout size is %.2f\n", (double)flow_total_num/req_total_num);
    printf("The average request DSCP value is %.2f\n", req_dscp_total/req_total_num);
    printf("The average request sending rate is %lu Mbps\n", req_rate_total/req_total_num);
    printf("The expected experiment duration is %lu s\n", req_interval_total/1000000);
}

/* receive traffic from established connections */
void *listen_connection(void *ptr)
{
    struct conn_node *node = (struct conn_node*)ptr;
    struct flow_metadata flow;
    char read_buf[TG_MAX_READ] = {0};
    int ret_val;
    while (true)
    {
        // printf("Reading flow metadata...\n");
        if (!read_flow_metadata(node->sockfd, &flow))
        {
            perror("Error: read meatadata");
            break;
        }
        // printf("Flow metadata read, flow id: %i\n", flow.id);
    
        // printf("Reading flow, flow id: %i\n", flow.id);
        if (flow.id==0)
        {
            ret_val = read_exact(node->sockfd, read_buf, flow.size, TG_MAX_READ, true);
        }
        else
            ret_val = read_exact_until(node->sockfd, read_buf, flow.size, TG_MAX_READ, true, &requests[flow_req_id[flow.id - 1]]);
        
        // printf("Reading complete, flow id: %i\n", flow.id);
        if (ret_val<0)
        {
            printf("Error, incomplete flow: %i\n", flow.id);
            perror("Error: receive flow");
            break;
        }
        else if ( ret_val!= flow.size)
        {
            // request already complete, kill flow by closing the connection
            if (flow.id!=0)
            {
                if (verbose_mode)
                    printf("Flow id: %i, killed prematurely\n",flow.id);
                gettimeofday(&requests[flow_req_id[flow.id - 1]].stop_time, NULL);
                pthread_mutex_lock(&(node->list->lock));

                node->connected = false;
                // printf("Closing socket\n");
                close(node->sockfd);
                //reopen the connection for later use
                // printf("Socket closed\n");
                // printf("Opening Socket\n");
                reinit_conn_node(node); //this will open a new socket and make connected = true
                // printf("Socket opened\n");
                pthread_mutex_unlock(&(node->list->lock));
            }
        }

        // flow completed
        node->busy = false;
        pthread_mutex_lock(&(node->list->lock));

        /* not the special flow ID */
        if (flow.id != 0)
        {
            node->list->flow_finished++;
            node->list->available_len++;
        }
        /* Ohterwise, it's a special flow ID to terminate connection.
           So this connection will no longer be available. */
        pthread_mutex_unlock(&(node->list->lock));

        /* a special flow ID to terminate persistent connection */
        if (flow.id == 0)
            break;
        else
        {
            gettimeofday(&flow_stop_time[flow.id - 1], NULL);
            pthread_mutex_lock(&(requests[flow_req_id[flow.id - 1]].lock));
            if ((requests[flow_req_id[flow.id - 1]].stop_time.tv_sec*1000000+requests[flow_req_id[flow.id - 1]].stop_time.tv_usec) == 0)
            {
                gettimeofday(&requests[flow_req_id[flow.id - 1]].stop_time, NULL);
                if (verbose_mode)
                    printf("fastest flow id: %i\n", flow.id );
            }
            pthread_mutex_unlock(&(requests[flow_req_id[flow.id - 1]].lock));
        }
    }
    pthread_mutex_lock(&(node->list->lock));
    close(node->sockfd);
    node->connected = false;
    node->busy = false;
    pthread_mutex_unlock(&(node->list->lock));

    return (void*)0;
}

/* generate rans requests */
void run_rans_requests()
{
    unsigned int i = 0;
    unsigned int k = 1;
    unsigned int req_duration_us = 0;
    int sleep_us = 0;
    struct timeval req_tv_start, req_tv_end;

    for (i = 0; i < req_total_num; i++)
    {
        gettimeofday(&req_tv_start, NULL);
        run_rans_request(i);
        gettimeofday(&req_tv_end, NULL);
        //duration to generate the current request
        req_duration_us = (req_tv_end.tv_sec - req_tv_start.tv_sec) * 1000000 + req_tv_end.tv_usec - req_tv_start.tv_usec;

        sleep_us = sleep_us + requests[i].sleep_us;
        if (sleep_us > usleep_overhead_us + req_duration_us)
        {
            usleep(sleep_us - usleep_overhead_us - req_duration_us);
            sleep_us = 0;
        }

        if (!verbose_mode && i + 1 >= k * req_total_num / 100)
        {
            display_progress(i + 1, req_total_num);
            k++;
        }
    }
    if (!verbose_mode)
        printf("\n");
}

/* generate a rans request to some servers */
void run_rans_request(unsigned int req_id)
{
    unsigned int conn_id, prio = 0;
    unsigned int i = 0;
    struct flow_request *flow_reqs = (struct flow_request*)malloc(requests[req_id].duplicates * sizeof(struct flow_request));
    pthread_t *threads = (pthread_t*)malloc(requests[req_id].duplicates * sizeof(pthread_t));
    struct conn_node *rans_server_conn = NULL;   /* server connection node */
    struct conn_node *tail_node = NULL;

    if (!flow_reqs || !threads)
    {
        perror("Error: malloc");
        free(flow_reqs);
        free(threads);
        return;
    }

    conn_id = 0;
    /* pre-establish all connections of this incast request*/
    for (i = 0; i < num_server; i++)
    {
        prio = requests[req_id].server_priorities[i];
        if (prio == 0)  /* no connection to this server */
            continue;

        if (connection_lists[i].available_len < 1)
        {
            tail_node = connection_lists[i].tail;
            /* establish 1 new connection */
            if (insert_conn_list(&connection_lists[i], 1))
            {
                /* start listen_connection thread on the newly established connection */
                while (true)
                {
                    tail_node = tail_node->next;
                    if (tail_node)
                        pthread_create(&(tail_node->thread), NULL, listen_connection, (void*)tail_node);
                    else
                        break;
                }

                if (verbose_mode)
                    printf("Established a new connection to %s:%u (available/total = %u/%u)\n", server_addr[i], server_port[i], connection_lists[i].available_len, connection_lists[i].len);
            }
            else
            {
                if (verbose_mode)
                    printf("Cannot establish a new connection to %s:%u (available/total = %u/%u)\n", server_addr[i], server_port[i], connection_lists[i].available_len, connection_lists[i].len);

                perror("Error: insert_conn_list");
                free(flow_reqs);
                free(threads);
                return;
            }
        }

        rans_server_conn = search_conn_list(&connection_lists[i]);
        if (rans_server_conn)
        {
           
            flow_reqs[conn_id].node = rans_server_conn;
            flow_reqs[conn_id].metadata.id = global_flow_id + 1; /* reserve flow ID 0 to terminate connections */
            flow_reqs[conn_id].metadata.size = requests[req_id].size;
            flow_reqs[conn_id].metadata.rate = requests[req_id].rate;
            if (requests[req_id].server_priorities[rans_server_conn->list->index]==1)
            {
                flow_reqs[conn_id].metadata.tos = 12;  /* ToS = 4 * DSCP */
            }
            else
            {
                flow_reqs[conn_id].metadata.tos = 0;  /* ToS = 4 * DSCP */
            }

            conn_id++;
            global_flow_id++;
        }
        else
        {
            perror("Error: search_conn_list");
            printf("search_conn_list, at request id: %i\n", req_id );
            free(flow_reqs);
            free(threads);
            return;
        }
    }

    if (conn_id != requests[req_id].duplicates)
    {
        perror("Error: no enough connections");
        free(flow_reqs);
        free(threads);
        return;
    }

    gettimeofday(&requests[req_id].start_time, NULL);
    /* generate requests to servers */
    for (i = 0; i < requests[req_id].duplicates; i++)
        pthread_create(&threads[i], NULL, run_flow, (void*)(&flow_reqs[i]));

    for (i = 0; i < requests[req_id].duplicates; i++)
        pthread_join(threads[i], NULL);

    free(flow_reqs);
    free(threads);
}

/* Generate a flow request to a server */
void *run_flow(void *ptr)
{
    struct flow_request f = *(struct flow_request*)ptr;
    struct conn_node *node = f.node;
    int sockfd = node->sockfd;

    /* Send request and record start time */
    if (f.metadata.id > 0)
    {
        gettimeofday(&flow_start_time[f.metadata.id - 1], NULL);
        if (verbose_mode)
            printf("flow id %i started at server %s : %u\n",f.metadata.id, f.node->list->ip, f.node->list->port); //Musa
    }

    node->busy = true;
    pthread_mutex_lock(&(node->list->lock));
    node->list->available_len--;
    pthread_mutex_unlock(&(node->list->lock));

    if (!write_flow_req(sockfd, &(f.metadata)))
        perror("Error: write metadata");

    return (void*)0;
}

/* terminate all existing connections */
void exit_connections()
{
    unsigned int i = 0;
    struct conn_node *ptr = NULL;
    unsigned int num = 0;

    /* start threads to receive traffic */
    for (i = 0; i < num_server; i++)
    {
        num = 0;
        ptr = connection_lists[i].head;
        while (true)
        {
            if (!ptr)
                break;
            else
            {
                if (ptr->connected)
                {
                    exit_connection(ptr);
                    num++;
                }
                ptr = ptr->next;
            }
        }
        wait_conn_list(&connection_lists[i]);
        if (verbose_mode)
            printf("Exit %u/%u connections to %s:%u\n", num, connection_lists[i].len, server_addr[i], server_port[i]);
    }
}

/* terminate a connection */
void exit_connection(struct conn_node *node)
{
    struct flow_request req;
    req.node = node;
    req.metadata.id = 0;
    req.metadata.size = 100;
    req.metadata.tos = 0;
    req.metadata.rate = 0;

    run_flow((void*)&req);
}

void print_statistic()
{
    unsigned long long duration_us = (tv_end.tv_sec - tv_start.tv_sec) * 1000000 + tv_end.tv_usec - tv_start.tv_usec;
    unsigned long long req_size_total = 0;
    unsigned long long fct_us, rct_us;
    unsigned int flow_goodput_mbps, req_goodput_mbps;   /* per-flow/request goodput (Mbps) */
    unsigned int goodput_mbps;  /* total goodput (Mbps) */
    unsigned int req_id;
    unsigned int i = 0;
    FILE *fd = NULL;

    fd = fopen(rct_log_name, "w");
    if (!fd)
    {
        cleanup();
        error("Error: open the RCT result file");
    }

    for (i = 0; i < req_total_num; i++)
    {
        req_size_total += requests[i].size;
        if ((requests[i].stop_time.tv_sec == 0) && (requests[i].stop_time.tv_usec == 0))
        {
            printf("Unfinished request %u\n", i);
            continue;
        }

        rct_us = (requests[i].stop_time.tv_sec - requests[i].start_time.tv_sec) * 1000000 + requests[i].stop_time.tv_usec - requests[i].start_time.tv_usec;
        if (rct_us > 0)
            req_goodput_mbps = requests[i].size * 8 / rct_us;
        else
            req_goodput_mbps = 0;

        /* request size, RCT(us), DSCP, sending rate (Mbps), goodput (Mbps), fanout */
        fprintf(fd, "%u %llu %u %u %u %u\n", requests[i].size, rct_us, requests[i].dscp, requests[i].rate, req_goodput_mbps, requests[i].duplicates);
    }
    fclose(fd);

    fd = fopen(fct_log_name, "w");
    if (!fd)
    {
        cleanup();
        error("Error: open the FCT result file");
    }

    for (i = 0; i < flow_total_num; i++)
    {
        if ((flow_stop_time[i].tv_sec == 0) && (flow_stop_time[i].tv_usec == 0))
        {
            printf("Unfinished flow %u\n", i);
            continue;
        }

        fct_us = (flow_stop_time[i].tv_sec - flow_start_time[i].tv_sec) * 1000000 + flow_stop_time[i].tv_usec - flow_start_time[i].tv_usec;
        req_id = flow_req_id[i];
        if (fct_us > 0)
            flow_goodput_mbps = requests[req_id].size / 8 / fct_us;
        else
            flow_goodput_mbps = 0;

        /* flow size, FCT(us), DSCP, sending rate (Mbps), goodput (Mbps) */
        fprintf(fd, "%u %llu %u %u %u\n", requests[req_id].size, fct_us, requests[req_id].dscp, requests[req_id].rate, flow_goodput_mbps);
    }
    fclose(fd);

    //******************** Musa *********************************
    // begin logging for start and end times
    fd = fopen("./times.txt", "w");
    if (!fd)
    {

        cleanup();
        error("Error: open the FCT result file");
    }

    for (i = 0; i < flow_total_num; i++)
    {
        if ((flow_stop_time[i].tv_sec == 0) && (flow_stop_time[i].tv_usec == 0))
        {
            printf("Unfinished flow %u\n", i);
            continue;
        }

        fct_us = (flow_stop_time[i].tv_sec - flow_start_time[i].tv_sec) * 1000000 + flow_stop_time[i].tv_usec - flow_start_time[i].tv_usec;
        req_id = flow_req_id[i];
        fprintf(fd, "%li %li %u %u\n", flow_start_time[i].tv_sec*1000000+flow_start_time[i].tv_usec, flow_stop_time[i].tv_sec*1000000+flow_stop_time[i].tv_usec, req_id, requests[req_id].dscp);
        /* flow size, FCT(us), DSCP, sending rate (Mbps), goodput (Mbps) */
        
    }
    fclose(fd);
    // end logging for start and end times
    //***********************************************************

    goodput_mbps = req_size_total * 8 / duration_us;
    printf("The actual RX throughput is %u Mbps\n", (unsigned int)(goodput_mbps/TG_GOODPUT_RATIO));
    printf("The actual duration is %llu s\n", duration_us/1000000);
    printf("===========================================\n");
    printf("Write RCT results to %s\n", rct_log_name);
    printf("Write FCT results to %s\n", fct_log_name);
}

/* clean up resources */
void cleanup()
{
    unsigned int i = 0;

    free(server_port);
    free(server_addr);
    free(server_flow_count);

    free(fanout_size);
    free(fanout_prob);

    free(dscp_value);
    free(dscp_prob);

    free(rate_value);
    free(rate_prob);

    free_cdf(req_size_dist);
    free(req_size_dist);

    if (requests)
    {
        for(i = 0; i < req_total_num; i++)
        {
            free(requests[i].server_priorities);
        }
    }

    free(requests);

    free(flow_req_id);
    free(flow_start_time);
    free(flow_stop_time);

    if (connection_lists)
    {
        if (verbose_mode)
            printf("===========================================\n");

        for(i = 0; i < num_server; i++)
        {
            if (verbose_mode)
                printf("Clear connection list %u to %s:%u\n", i, connection_lists[i].ip, connection_lists[i].port);
            clear_conn_list(&connection_lists[i]);
        }
    }
    free(connection_lists);
}
