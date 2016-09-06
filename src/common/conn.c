#include "conn.h"
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

/* initialize connection */
bool init_conn_node(struct conn_node *node, int id, struct conn_list *list)
{
    struct sockaddr_in serv_addr;
    int sock_opt = 1;

    if (!node)
        return false;

    node->id = id;
    node->busy = false;
    node->next = NULL;
    node->list = list;
    node->connected = false;

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(list->ip);
    serv_addr.sin_port = htons(list->port);

    /* initialize server socket */
    node->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (node->sockfd < 0)
    {
        char msg[256] = {0};
        snprintf(msg, 256, "Error: init socket (to %s:%hu) in init_conn_node()", list->ip, list->port);
        perror(msg);
        return false;
    }

    /* set socket options */
    if (setsockopt(node->sockfd, SOL_SOCKET, SO_REUSEADDR, &sock_opt, sizeof(sock_opt)) < 0)
    {
        char msg[256] = {0};
        snprintf(msg, 256, "Error: set SO_REUSEADDR (to %s:%hu) in init_conn_node()", list->ip, list->port);
        perror(msg);
        return false;
    }
    if (setsockopt(node->sockfd, IPPROTO_TCP, TCP_NODELAY, &sock_opt, sizeof(sock_opt)) < 0)
    {
        char msg[256] = {0};
        snprintf(msg, 256, "Error: set TCP_NODELAY (to %s:%hu) in init_conn_node()", list->ip, list->port);
        perror(msg);
        return false;
    }
    struct linger so_linger;
    so_linger.l_onoff = 1;
    so_linger.l_linger = 0; //causes close to immediately abort the connection without attempting to deliver pending data
    if (setsockopt(node->sockfd, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger)) < 0)
    {
        char msg[256] = {0};
        snprintf(msg, 256, "Error: set SO_LINGER (to %s:%hu) in init_conn_node()", list->ip, list->port);
        perror(msg);
        return false;
    }

    if (connect(node->sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
    {
        char msg[256] = {0};
        snprintf(msg, 256, "Error: connect() (to %s:%hu) in init_conn_node()", list->ip, list->port);
        perror(msg);
        return false;
    }

    node->connected = true;
    return true;
}

bool init_conn_list(struct conn_list *list, int index, char *ip, unsigned short port)
{
    if (!list)
        return false;

    if (strlen(ip) < sizeof(list->ip))
    {
        strcpy(list->ip, ip);
        list->ip[strlen(list->ip)] = '\0';
    }
    else
        return false;

    list->index = index;
    list->port = port;
    list->head = NULL;
    list->tail = NULL;
    list->len = 0;
    list->available_len = 0;
    list->flow_finished = 0;
    pthread_mutex_init(&(list->lock), NULL);

    return true;
}

/* insert several nodes to the tail of the linked list */
bool insert_conn_list(struct conn_list *list, int num)
{
    int i = 0;
    struct conn_node *new_node = NULL;

    if (!list)
        return false;

    for (i = 0; i < num; i++)
    {
        new_node = (struct conn_node*)malloc(sizeof(struct conn_node));
        if (!init_conn_node(new_node, list->len, list))
        {
            free(new_node);
            return false;
        }

        /* if the list is empty */
        if (list->len == 0)
        {
            list->head = new_node;
            list->tail = new_node;
        }
        else
        {
            list->tail->next = new_node;
            list->tail = new_node;
        }
        pthread_mutex_lock(&(list->lock));
        list->len++;
        list->available_len++;
        pthread_mutex_unlock(&(list->lock));
    }

    return true;
}

/* search the first available connection (busy==false && connected==true) in the list. */
struct conn_node *search_conn_list(struct conn_list *list)
{
    struct conn_node *ptr = NULL;

    if (!list || !(list->available_len))
        return NULL;

    ptr = list->head;
    while (true)
    {
        if (!ptr)
        {
            return NULL;
        }

        pthread_mutex_lock(&(ptr->lock));
        if (!(ptr->busy) && ptr->connected)
        {
            pthread_mutex_unlock(&(ptr->lock));
            return ptr;
        }
        pthread_mutex_unlock(&(ptr->lock));

        ptr = ptr->next;
        
    }

    return NULL;
}

/* search N available connections in the list */
struct conn_node **search_n_conn_list(struct conn_list *list, unsigned int num)
{
    struct conn_node *ptr = NULL;
    struct conn_node **result = NULL;
    int i = 0;
    if (!list || list->available_len < num || !num)
        return NULL;

    result = (struct conn_node**)malloc(num * sizeof(struct conn_node*));
    if (!result)
    {
        perror("Error: malloc result in search_n_conn_list()");
        return NULL;
    }

    ptr = list->head;
    while (true)
    {
        if (ptr)
        {
            if (!(ptr->busy) && ptr->connected)
                result[i++] = ptr;

            if (i < num)
                ptr = ptr->next;
            else
            {
                return result;
            }
        }
        else
        {
            printf("linked list pointer is null before reqd connections are found!\n");
            perror("Error: required connections not found in search_n_conn_list()");
            free(result);
            return NULL;
        }
    }

    return NULL;
}

/* wait for all threads in the linked list to finish */
void wait_conn_list(struct conn_list *list)
{
    // printf("Waiting for connection threads to terminate\n");
    struct conn_node *ptr = NULL;
    struct timespec ts;
    int s;

    if (!list)
        return;

    ptr = list->head;
    while (true)
    {
        if (!ptr)
            break;
        else
        {
            /* if this connection is active, we need to wait for long enough time */
            // pthread_mutex_lock(&(ptr->lock));
            // if (ptr->connected)
            // {
            //     pthread_mutex_unlock(&(ptr->lock));
            //     printf("Waiting for thread to finish, conn id: %i\n", ptr->id);
            //     s = pthread_join(ptr->thread, NULL);
            //     if (s != 0)
            //     {
            //         char msg[256] = {0};
            //         snprintf(msg, 256, "Error: pthread_join() (to %s:%hu) in wait_conn_list()",
            //                  list->ip, list->port);
            //         perror(msg);
            //     }
            // }
            // else
            // {
                // pthread_mutex_unlock(&(ptr->lock));
            /* commented the above as some threads do not terminate - Musa (TODO: Fix this)*/
                printf("Waiting for thread to timeout, conn id: %i\n", ptr->id);
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_sec += 5;
                s = pthread_timedjoin_np(ptr->thread, NULL, &ts);
                if (s != 0)
                {
                    char msg[256] = {0};
                    snprintf(msg, 256, "Error: pthread_timedjoin_np() (to %s:%hu) in wait_conn_list()",
                             list->ip, list->port);
                    perror(msg);
                }
            // }
            ptr = ptr->next;
        }
    }
}

/* clear all the nodes in the linked list */
void clear_conn_list(struct conn_list *list)
{
    struct conn_node *ptr = NULL;
    struct conn_node *next_node = NULL;

    if (!list)
        return;

    for (ptr = list->head; ptr != NULL; ptr = next_node)
    {
        next_node = ptr->next;
        free(ptr);
    }

    list->len = 0;
}

/* print information of the linked list */
void print_conn_list(struct conn_list *list)
{
    if (list)
        printf("%s:%hu  total connections: %u  available connections: %u  flows finished: %u\n",
               list->ip, list->port, list->len, list->available_len, list->flow_finished);
}

struct conn_node *search_conn_list_by_nodeid(struct conn_list *list, int id)
{
    struct conn_node *ptr = NULL;

    if (!list || !(list->available_len))
        return NULL;

    ptr = list->head;
    while (true)
    {
        if (!ptr)
            return NULL;
        else if ((ptr->id==id))
            return ptr;
        else
            ptr = ptr->next;
    }

    return NULL;
}


// /* remove a node from the list - Musa */
// bool remove_from_conn_list(struct conn_list *list, struct conn_node *node)
// {
//     struct conn_node *ptr = list->head;
//     if (node==list->head)
//     {
//         if (node==list->tail)
//         {
//             free(list->head);
//             list->head=NULL;
//             list->tail=NULL;
//             return true;
//         }
//         else
//         {
//             if (list->head->next!=NULL) //this must always be true (as tail and head dont point at the same node)
//             {
//                 list->head = list->head->next;
//                 free(ptr);
//                 return true;
//             }
//             return false; //this line never runs
//         }
//          /* code */
//     }
//     else if (node==list->head)
//     {
//         /* code */
//     }
// }


// // close connection - unclean
// bool close_conn_node(struct conn_node *node)
// {
//     return close(node->sockfd);
// }

// //restarting a closed connection - unclean
// bool reinit_conn_node(struct conn_node *node)
// {
//     return init_conn_node(node, node->id, node->list);
// }



bool reinit_conn_node(struct conn_node *node)
{
    struct sockaddr_in serv_addr;
    int sock_opt = 1;

    if (!node)
        return false;

    // pthread_mutex_lock(&(node->lock));
    node->connected = false;
    // pthread_mutex_unlock(&(node->lock));

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(node->list->ip);
    serv_addr.sin_port = htons(node->list->port);

    /* initialize server socket */
    node->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (node->sockfd < 0)
    {
        char msg[256] = {0};
        snprintf(msg, 256, "Error: init socket (to %s:%hu) in init_conn_node()", node->list->ip, node->list->port);
        perror(msg);
        return false;
    }
 
    /* set socket options */
    if (setsockopt(node->sockfd, SOL_SOCKET, SO_REUSEADDR, &sock_opt, sizeof(sock_opt)) < 0)
    {
        char msg[256] = {0};
        snprintf(msg, 256, "Error: set SO_REUSEADDR (to %s:%hu) in init_conn_node()", node->list->ip, node->list->port);
        perror(msg);
        return false;
    }
    if (setsockopt(node->sockfd, IPPROTO_TCP, TCP_NODELAY, &sock_opt, sizeof(sock_opt)) < 0)
    {
        char msg[256] = {0};
        snprintf(msg, 256, "Error: set TCP_NODELAY (to %s:%hu) in init_conn_node()", node->list->ip, node->list->port);
        perror(msg);
        return false;
    }
    struct linger so_linger;
    so_linger.l_onoff = 1;
    so_linger.l_linger = 0;
    if (setsockopt(node->sockfd, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger)) < 0)
    {
        char msg[256] = {0};
        snprintf(msg, 256, "Error: set SO_LINGER (to %s:%hu) in init_conn_node()", node->list->ip, node->list->port);
        perror(msg);
        return false;
    }

    if (connect(node->sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
    {
        char msg[256] = {0};
        snprintf(msg, 256, "Error: connect() (to %s:%hu) in init_conn_node()", node->list->ip, node->list->port);
        perror(msg);
        return false;
    }

    // pthread_mutex_lock(&(node->lock));
    node->connected = true;
    // pthread_mutex_unlock(&(node->lock));
    return true;
}