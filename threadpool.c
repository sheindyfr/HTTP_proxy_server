#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <zconf.h>
#include "threadpool.h"
int append(threadpool *pool, work_t* work);
work_t *pop_first(threadpool *pool);
void usage_error()
{
    printf("Usage: threadpool <pool-size> <max-number-of-jobs>\n");
    exit(EXIT_FAILURE);
}
void error(char* msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

threadpool* create_threadpool(int num_threads_in_pool)
{
    int i;
    if(num_threads_in_pool>MAXT_IN_POOL || num_threads_in_pool<=0)
        usage_error();
    threadpool *pool=malloc(sizeof(threadpool));
    if(pool == NULL)
        error("ERROR malloc");
    pool->num_threads=num_threads_in_pool;
    pool->qsize=0;
    pool->threads=malloc(num_threads_in_pool * sizeof(pthread_t));
    if(pool->threads == NULL)
    {
        free(pool);
        error("ERROR malloc");
    }
    pool->qhead=NULL;
    pool->qtail=NULL;
    pool->shutdown=0;
    pool->dont_accept=0;
    pthread_mutex_init(&pool->qlock, NULL);
    pthread_cond_init(&pool->q_not_empty, NULL);
    pthread_cond_init(&pool->q_empty, NULL);

    for(i=0; i < pool->num_threads; ++i)
    {
        pthread_create(pool->threads + i, NULL, do_work, (void*)pool);
    }
    return pool;
}
void dispatch(threadpool* from_me, dispatch_fn dispatch_to_here, void *arg)
{
    work_t *work;
    if(from_me->dont_accept)
        return;
    work=malloc(sizeof(work_t));
    if(work==NULL)
    {
        destroy_threadpool(from_me);
        error("ERROR malloc");
    }
    work->arg=arg;
    work->routine=dispatch_to_here;
    pthread_mutex_lock(&from_me->qlock);
    if(append(from_me, work)<0) //add a job to the queue
    {
        destroy_threadpool(from_me);
        exit(EXIT_FAILURE);
    }
    pthread_cond_signal(&from_me->q_not_empty);
    pthread_mutex_unlock(&from_me->qlock);
}

int append(threadpool *pool, work_t* work)
{
    if(pool==NULL || work==NULL)
        return -1;
    work->next=NULL;
    if (pool->qhead == NULL)
        pool->qhead=work;
    else pool->qtail->next=work;
    pool->qtail=work;
    pool->qsize++;
    return 0;
}
work_t *pop_first(threadpool *pool)
{
    work_t* tmp;
    if (pool == NULL)
        return NULL;
    tmp = pool->qhead;
    if (tmp == NULL)
        return NULL;
    pool->qhead = pool->qhead->next;
    if(pool->qhead==NULL)
        pool->qtail=NULL;
    pool->qsize--;
    return tmp;
}
void* do_work(void* p)
{
    threadpool *pool;
    work_t *to_do;
    pool=(threadpool*)p;
    while(1)
    {
        pthread_mutex_lock(&pool->qlock);
        if(pool->shutdown)
        {
            pthread_mutex_unlock(&pool->qlock);
            return NULL;
        }

        if(pool->qsize==0)
            pthread_cond_wait(&pool->q_not_empty, &pool->qlock);
        if(pool->shutdown)
        {
            pthread_mutex_unlock(&pool->qlock);
            return NULL;
        }
        if(pool->qsize==0)
            continue;
        to_do=pop_first(pool);  //take a job from the queue
        if(pool->dont_accept && pool->qsize==0)
        {
            pthread_cond_signal(&pool->q_empty);
        }
        pthread_mutex_unlock(&pool->qlock);
        (to_do->routine)(to_do->arg);
        free(to_do);
    }
}
void destroy_threadpool(threadpool* destroyme)
{
    pthread_mutex_lock(&destroyme->qlock);
    destroyme->dont_accept=1;
    pthread_cond_wait(&destroyme->q_empty, &destroyme->qlock);
    destroyme->shutdown=1;
    pthread_cond_broadcast(&destroyme->q_not_empty);
    pthread_mutex_unlock(&destroyme->qlock);
    for(int i=0; i<destroyme->num_threads; i++)
    {
        pthread_join(destroyme->threads[i], NULL);
    }
    free(destroyme->threads);
    free(destroyme);
}
