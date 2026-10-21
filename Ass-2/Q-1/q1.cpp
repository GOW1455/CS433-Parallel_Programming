#include <iostream>
#include <cstdlib>
#include <vector>
#include <pthread.h>
#include <semaphore.h>
#include <sys/time.h>

using namespace std;

// Usage: ./q1 <N> <P> <n>
// P1=lock-free semaphore | P2=POSIX sem_t | P3=condition variable

unsigned char CAS(int old_val, int new_val, int *ptr)
{
    int out; unsigned char res;
    asm("lock cmpxchgl %4, %1\nsetzb %0"
        : "=qm"(res), "+m"(*ptr), "=a"(out)
        : "a"(old_val), "r"(new_val) :);
    return res;
}

int fetch_and_add(volatile int *p, int d)
{
    int old;
    do { old = *p; } while(!CAS(old, old + d, (int*)p));
    return old;
}

// lock-free FIFO semaphore
// negative count = that many threads are blocked
// ticket-based queue: tail assigns slots, head wakes them in order
#define MAXW 512

struct LFSem
{
    volatile int count; char pad1[60];
    volatile int tail;  char pad2[60];
    volatile int head;  char pad3[60];
    volatile int wake[MAXW];
};

void lf_sem_init(LFSem *s, int v)
{
    s->count = v; s->tail = 0; s->head = 0;
    for(int i = 0; i < MAXW; i++) s->wake[i] = 0;
}

void lf_sem_wait(LFSem *s)
{
    if(fetch_and_add(&s->count, -1) > 0) return;
    int slot = fetch_and_add(&s->tail, 1) % MAXW;
    while(!s->wake[slot]);
    s->wake[slot] = 0;
}

void lf_sem_post(LFSem *s)
{
    if(fetch_and_add(&s->count, 1) >= 0) return;
    int slot = fetch_and_add(&s->head, 1) % MAXW;
    s->wake[slot] = 1;
}

// shared ring buffer
struct alignas(64) Slot
{
    volatile int value;
    volatile int full;  // 0=empty, 1=written
};

int buf_size, num_prod, num_vals;
Slot *buf;
volatile int write_ptr;
int read_ptr;
volatile long checksum;

void reset_buf()
{
    write_ptr = 0; read_ptr = 0; checksum = 0;
    for(int i = 0; i < buf_size; i++) { buf[i].value = 0; buf[i].full = 0; }
}

// P1: lock-free semaphore
LFSem p1_empty, p1_full;

void *p1_producer(void *arg)
{
    int start = *(int*)arg;
    for(int i = 0; i < num_vals; i++)
    {
        lf_sem_wait(&p1_empty);
        int idx = fetch_and_add(&write_ptr, 1) % buf_size;
        buf[idx].value = start + i;
        buf[idx].full = 1;
        lf_sem_post(&p1_full);
    }
    return NULL;
}

void *p1_consumer(void *arg)
{
    int total = num_prod * num_vals;
    long sum = 0;
    for(int i = 0; i < total; i++)
    {
        lf_sem_wait(&p1_full);
        while(!buf[read_ptr].full);  // spin: producers claim slots out of order
        int val = buf[read_ptr].value;
        printf("%d ", val);
        sum += val;
        buf[read_ptr].full = 0;
        read_ptr = (read_ptr + 1) % buf_size;
        lf_sem_post(&p1_empty);
    }
    printf("\n");
    checksum = sum;
    return NULL;
}

// P2: POSIX semaphore
sem_t p2_empty, p2_full;

void *p2_producer(void *arg)
{
    int start = *(int*)arg;
    for(int i = 0; i < num_vals; i++)
    {
        sem_wait(&p2_empty);
        int idx = fetch_and_add(&write_ptr, 1) % buf_size;
        buf[idx].value = start + i;
        buf[idx].full = 1;
        sem_post(&p2_full);
    }
    return NULL;
}

void *p2_consumer(void *arg)
{
    int total = num_prod * num_vals;
    long sum = 0;
    for(int i = 0; i < total; i++)
    {
        sem_wait(&p2_full);
        while(!buf[read_ptr].full);  // spin: producers claim slots out of order
        int val = buf[read_ptr].value;
        printf("%d ", val);
        sum += val;
        buf[read_ptr].full = 0;
        read_ptr = (read_ptr + 1) % buf_size;
        sem_post(&p2_empty);
    }
    printf("\n");
    checksum = sum;
    return NULL;
}

// P3: condition variable
pthread_mutex_t p3_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t p3_not_empty = PTHREAD_COND_INITIALIZER;
pthread_cond_t p3_not_full = PTHREAD_COND_INITIALIZER;
int p3_count;
int p3_wptr;

void *p3_producer(void *arg)
{
    int start = *(int*)arg;
    for(int i = 0; i < num_vals; i++)
    {
        pthread_mutex_lock(&p3_mutex);
        while(p3_count == buf_size)
            pthread_cond_wait(&p3_not_full, &p3_mutex);
        int idx = p3_wptr % buf_size;
        p3_wptr++;
        buf[idx].value = start + i;
        buf[idx].full = 1;
        p3_count++;
        pthread_cond_signal(&p3_not_empty);
        pthread_mutex_unlock(&p3_mutex);
    }
    return NULL;
}

void *p3_consumer(void *arg)
{
    int total = num_prod * num_vals;
    long sum = 0;
    for(int i = 0; i < total; i++)
    {
        pthread_mutex_lock(&p3_mutex);
        while(p3_count == 0)
            pthread_cond_wait(&p3_not_empty, &p3_mutex);
        int val = buf[read_ptr].value;
        printf("%d ", val);
        sum += val;
        buf[read_ptr].full = 0;
        read_ptr = (read_ptr + 1) % buf_size;
        p3_count--;
        pthread_cond_signal(&p3_not_full);
        pthread_mutex_unlock(&p3_mutex);
    }
    printf("\n");
    checksum = sum;
    return NULL;
}

long run_program(void *(*prod_fn)(void*), void *(*cons_fn)(void*), int *starts)
{
    vector<pthread_t> prod_tids(num_prod);
    pthread_t cons_tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);

    struct timeval t0, t1;
    struct timezone tz;
    gettimeofday(&t0, &tz);

    pthread_create(&cons_tid, &attr, cons_fn, NULL);
    for(int i = 0; i < num_prod; i++)
        pthread_create(&prod_tids[i], &attr, prod_fn, &starts[i]);
    for(int i = 0; i < num_prod; i++)
        pthread_join(prod_tids[i], NULL);
    pthread_join(cons_tid, NULL);

    gettimeofday(&t1, &tz);
    return (t1.tv_sec - t0.tv_sec) * 1000000L + (t1.tv_usec - t0.tv_usec);
}

int main(int argc, char *argv[])
{
    if(argc != 4)
    {
        cerr << "Usage: " << argv[0] << " <N> <P> <n>" << endl;
        return 1;
    }

    buf_size = atoi(argv[1]);
    num_prod = atoi(argv[2]);
    num_vals = atoi(argv[3]);

    buf = new Slot[buf_size];

    vector<int> starts(num_prod);
    for(int i = 0; i < num_prod; i++) starts[i] = i * num_vals;

    long t1, t2, t3;

    // P1
    reset_buf();
    lf_sem_init(&p1_empty, buf_size);
    lf_sem_init(&p1_full, 0);
    t1 = run_program(p1_producer, p1_consumer, starts.data());
    fprintf(stderr, "P1 (lock-free sem) : Time = %ld us, checksum = %ld\n", t1, (long)checksum);

    // P2
    reset_buf();
    sem_init(&p2_empty, 0, buf_size);
    sem_init(&p2_full, 0, 0);
    t2 = run_program(p2_producer, p2_consumer, starts.data());
    sem_destroy(&p2_empty);
    sem_destroy(&p2_full);
    fprintf(stderr, "P2 (POSIX sem)     : Time = %ld us, checksum = %ld\n", t2, (long)checksum);

    // P3
    reset_buf();
    p3_count = 0; p3_wptr = 0;
    t3 = run_program(p3_producer, p3_consumer, starts.data());
    fprintf(stderr, "P3 (cond var)      : Time = %ld us, checksum = %ld\n", t3, (long)checksum);

    fprintf(stderr, "\nN=%d P=%d n=%d  total=%d items\n", buf_size, num_prod, num_vals, num_prod * num_vals);

    delete[] buf;
    return 0;
}
