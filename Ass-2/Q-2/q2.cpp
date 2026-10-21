#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstdlib>
#include <climits>
#include <pthread.h>
#include <sys/time.h>

using namespace std;

// Usage: ./q2 <graph_file> <ops_file> <num_ops> <num_threads>
// P1=global mutex | P2=per-list mutex | P3=per-node mutex (HOH) |
// P4=per-node rwlock (HOH) | P5=per-node CAS lock (HOH) | P6=optimistic CAS

#define OP_INSERT 0
#define OP_DELETE 1
#define OP_SEARCH 2

int num_threads, num_ops, N;

struct op_t { int op, u, v; };
vector<op_t> ops;

// x86 CAS lock using lock cmpxchgl / setzb (same as class CompareAndSet)
unsigned char CAS(int old_val, int new_val, int *ptr)
{
    int out; unsigned char res;
    asm("lock cmpxchgl %4, %1 \n setzb %0"
        : "=qm"(res), "+m"(*ptr), "=a"(out)
        : "a"(old_val), "r"(new_val) :);
    return res;
}

void acquire(int *lk) { while(!CAS(0, 1, lk)); }
void release(int *lk) { *lk = 0; }

long run_threads(void *(*fn)(void*))
{
    vector<pthread_t> tids(num_threads);
    vector<int> ids(num_threads);
    for(int i = 0; i < num_threads; i++) ids[i] = i;

    pthread_attr_t attr;
    pthread_attr_init(&attr);

    struct timeval t0, t1;
    struct timezone tz;
    gettimeofday(&t0, &tz);

    for(int i = 1; i < num_threads; i++)
        pthread_create(&tids[i], &attr, fn, &ids[i]);
    fn(&ids[0]);
    for(int i = 1; i < num_threads; i++)
        pthread_join(tids[i], NULL);

    gettimeofday(&t1, &tz);
    return (t1.tv_sec - t0.tv_sec) * 1000000L + (t1.tv_usec - t0.tv_usec);
}

// P1: single global POSIX mutex
struct Node1 { int val; Node1 *next; };
vector<Node1*> adj1;
pthread_mutex_t global_lock = PTHREAD_MUTEX_INITIALIZER;

void seq_insert1(int u, int v)
{
    Node1 *curr = adj1[u], *prev = NULL;
    while(curr && curr->val < v) { prev = curr; curr = curr->next; }
    if(curr && curr->val == v) return;
    Node1 *node = new Node1{v, curr};
    if(prev) prev->next = node; else adj1[u] = node;
}

int insert1(int u, int v)
{
    pthread_mutex_lock(&global_lock);
    Node1 *curr = adj1[u], *prev = NULL;
    while(curr && curr->val < v) { prev = curr; curr = curr->next; }
    if(curr && curr->val == v) { pthread_mutex_unlock(&global_lock); return 0; }
    Node1 *node = new Node1{v, curr};
    if(prev) prev->next = node; else adj1[u] = node;
    pthread_mutex_unlock(&global_lock);
    return 1;
}

int delete1(int u, int v)
{
    pthread_mutex_lock(&global_lock);
    Node1 *curr = adj1[u], *prev = NULL;
    while(curr && curr->val < v) { prev = curr; curr = curr->next; }
    if(curr && curr->val == v)
    {
        if(prev) prev->next = curr->next; else adj1[u] = curr->next;
        delete curr;
        pthread_mutex_unlock(&global_lock);
        return 1;
    }
    pthread_mutex_unlock(&global_lock);
    return 0;
}

int search1(int u, int v)
{
    pthread_mutex_lock(&global_lock);
    Node1 *curr = adj1[u];
    while(curr && curr->val < v) curr = curr->next;
    int ret = (curr && curr->val == v) ? 1 : 0;
    pthread_mutex_unlock(&global_lock);
    return ret;
}

void *fn1(void *arg)
{
    int id = *(int*)arg, dummy = 0;
    for(int i = (num_ops/num_threads)*id; i < (num_ops/num_threads)*(id+1); i++)
    {
        const op_t& op = ops[i];
        if(op.op == OP_INSERT)      dummy += insert1(op.u, op.v);
        else if(op.op == OP_DELETE) dummy += delete1(op.u, op.v);
        else                        dummy += search1(op.u, op.v);
    }
    return NULL;
}

// P2: per-list mutex (one mutex per adjacency list row)
struct Node2 { int val; Node2 *next; };
vector<Node2*> adj2;
vector<pthread_mutex_t> row_locks2;

void init_adj2()
{
    adj2.resize(N, NULL);
    row_locks2.resize(N);
    for(int i = 0; i < N; i++)
        pthread_mutex_init(&row_locks2[i], NULL);
}

void seq_insert2(int u, int v)
{
    Node2 *curr = adj2[u], *prev = NULL;
    while(curr && curr->val < v) { prev = curr; curr = curr->next; }
    if(curr && curr->val == v) return;
    Node2 *node = new Node2{v, curr};
    if(prev) prev->next = node; else adj2[u] = node;
}

int insert2(int u, int v)
{
    pthread_mutex_lock(&row_locks2[u]);
    Node2 *curr = adj2[u], *prev = NULL;
    while(curr && curr->val < v) { prev = curr; curr = curr->next; }
    if(curr && curr->val == v) { pthread_mutex_unlock(&row_locks2[u]); return 0; }
    Node2 *node = new Node2{v, curr};
    if(prev) prev->next = node; else adj2[u] = node;
    pthread_mutex_unlock(&row_locks2[u]);
    return 1;
}

int delete2(int u, int v)
{
    pthread_mutex_lock(&row_locks2[u]);
    Node2 *curr = adj2[u], *prev = NULL;
    while(curr && curr->val < v) { prev = curr; curr = curr->next; }
    if(curr && curr->val == v)
    {
        if(prev) prev->next = curr->next; else adj2[u] = curr->next;
        delete curr;
        pthread_mutex_unlock(&row_locks2[u]);
        return 1;
    }
    pthread_mutex_unlock(&row_locks2[u]);
    return 0;
}

int search2(int u, int v)
{
    pthread_mutex_lock(&row_locks2[u]);
    Node2 *curr = adj2[u];
    while(curr && curr->val < v) curr = curr->next;
    int ret = (curr && curr->val == v) ? 1 : 0;
    pthread_mutex_unlock(&row_locks2[u]);
    return ret;
}

void *fn2(void *arg)
{
    int id = *(int*)arg, dummy = 0;
    for(int i = (num_ops/num_threads)*id; i < (num_ops/num_threads)*(id+1); i++)
    {
        const op_t& op = ops[i];
        if(op.op == OP_INSERT)      dummy += insert2(op.u, op.v);
        else if(op.op == OP_DELETE) dummy += delete2(op.u, op.v);
        else                        dummy += search2(op.u, op.v);
    }
    return NULL;
}

// P3: per-node mutex, hand-over-hand; sentinel head (val=INT_MIN) per row
struct Node3 { int val; pthread_mutex_t lock; Node3 *next; };
vector<Node3*> adj3;

void init_adj3()
{
    adj3.resize(N);
    for(int i = 0; i < N; i++)
    {
        adj3[i] = new Node3();
        adj3[i]->val  = INT_MIN;
        adj3[i]->next = NULL;
        pthread_mutex_init(&adj3[i]->lock, NULL);
    }
}

void seq_insert3(int u, int v)
{
    Node3 *prev = adj3[u], *curr = prev->next;
    while(curr && curr->val < v) { prev = curr; curr = curr->next; }
    if(curr && curr->val == v) return;
    Node3 *node = new Node3();
    node->val  = v; node->next = curr;
    pthread_mutex_init(&node->lock, NULL);
    prev->next = node;
}

int insert3(int u, int v)
{
    Node3 *prev = adj3[u], *curr;
    pthread_mutex_lock(&prev->lock);
    curr = prev->next;
    if(curr) pthread_mutex_lock(&curr->lock);
    while(curr && curr->val < v)
    {
        pthread_mutex_unlock(&prev->lock);
        prev = curr; curr = curr->next;
        if(curr) pthread_mutex_lock(&curr->lock);
    }
    if(curr && curr->val == v)
    {
        pthread_mutex_unlock(&curr->lock);
        pthread_mutex_unlock(&prev->lock);
        return 0;
    }
    Node3 *node = new Node3();
    node->val  = v; node->next = curr;
    pthread_mutex_init(&node->lock, NULL);
    prev->next = node;
    if(curr) pthread_mutex_unlock(&curr->lock);
    pthread_mutex_unlock(&prev->lock);
    return 1;
}

int delete3(int u, int v)
{
    Node3 *prev = adj3[u], *curr;
    pthread_mutex_lock(&prev->lock);
    curr = prev->next;
    if(curr) pthread_mutex_lock(&curr->lock);
    while(curr && curr->val < v)
    {
        pthread_mutex_unlock(&prev->lock);
        prev = curr; curr = curr->next;
        if(curr) pthread_mutex_lock(&curr->lock);
    }
    if(curr && curr->val == v)
    {
        prev->next = curr->next;
        pthread_mutex_unlock(&curr->lock);
        delete curr;
        pthread_mutex_unlock(&prev->lock);
        return 1;
    }
    if(curr) pthread_mutex_unlock(&curr->lock);
    pthread_mutex_unlock(&prev->lock);
    return 0;
}

int search3(int u, int v)
{
    Node3 *prev = adj3[u], *curr;
    pthread_mutex_lock(&prev->lock);
    curr = prev->next;
    if(curr) pthread_mutex_lock(&curr->lock);
    while(curr && curr->val < v)
    {
        pthread_mutex_unlock(&prev->lock);
        prev = curr; curr = curr->next;
        if(curr) pthread_mutex_lock(&curr->lock);
    }
    int ret = (curr && curr->val == v) ? 1 : 0;
    if(curr) pthread_mutex_unlock(&curr->lock);
    pthread_mutex_unlock(&prev->lock);
    return ret;
}

void *fn3(void *arg)
{
    int id = *(int*)arg, dummy = 0;
    for(int i = (num_ops/num_threads)*id; i < (num_ops/num_threads)*(id+1); i++)
    {
        const op_t& op = ops[i];
        if(op.op == OP_INSERT)      dummy += insert3(op.u, op.v);
        else if(op.op == OP_DELETE) dummy += delete3(op.u, op.v);
        else                        dummy += search3(op.u, op.v);
    }
    return NULL;
}

// P4: per-node RW lock, hand-over-hand; rdlock for search, wrlock for insert/delete
struct Node4 { int val; pthread_rwlock_t lock; Node4 *next; };
vector<Node4*> adj4;

void init_adj4()
{
    adj4.resize(N);
    for(int i = 0; i < N; i++)
    {
        adj4[i] = new Node4();
        adj4[i]->val  = INT_MIN;
        adj4[i]->next = NULL;
        pthread_rwlock_init(&adj4[i]->lock, NULL);
    }
}

void seq_insert4(int u, int v)
{
    Node4 *prev = adj4[u], *curr = prev->next;
    while(curr && curr->val < v) { prev = curr; curr = curr->next; }
    if(curr && curr->val == v) return;
    Node4 *node = new Node4();
    node->val  = v; node->next = curr;
    pthread_rwlock_init(&node->lock, NULL);
    prev->next = node;
}

int insert4(int u, int v)
{
    Node4 *prev = adj4[u], *curr;
    pthread_rwlock_wrlock(&prev->lock);
    curr = prev->next;
    if(curr) pthread_rwlock_wrlock(&curr->lock);
    while(curr && curr->val < v)
    {
        pthread_rwlock_unlock(&prev->lock);
        prev = curr; curr = curr->next;
        if(curr) pthread_rwlock_wrlock(&curr->lock);
    }
    if(curr && curr->val == v)
    {
        pthread_rwlock_unlock(&curr->lock);
        pthread_rwlock_unlock(&prev->lock);
        return 0;
    }
    Node4 *node = new Node4();
    node->val  = v; node->next = curr;
    pthread_rwlock_init(&node->lock, NULL);
    prev->next = node;
    if(curr) pthread_rwlock_unlock(&curr->lock);
    pthread_rwlock_unlock(&prev->lock);
    return 1;
}

int delete4(int u, int v)
{
    Node4 *prev = adj4[u], *curr;
    pthread_rwlock_wrlock(&prev->lock);
    curr = prev->next;
    if(curr) pthread_rwlock_wrlock(&curr->lock);
    while(curr && curr->val < v)
    {
        pthread_rwlock_unlock(&prev->lock);
        prev = curr; curr = curr->next;
        if(curr) pthread_rwlock_wrlock(&curr->lock);
    }
    if(curr && curr->val == v)
    {
        prev->next = curr->next;
        pthread_rwlock_unlock(&curr->lock);
        delete curr;
        pthread_rwlock_unlock(&prev->lock);
        return 1;
    }
    if(curr) pthread_rwlock_unlock(&curr->lock);
    pthread_rwlock_unlock(&prev->lock);
    return 0;
}

int search4(int u, int v)
{
    Node4 *prev = adj4[u], *curr;
    pthread_rwlock_rdlock(&prev->lock);
    curr = prev->next;
    if(curr) pthread_rwlock_rdlock(&curr->lock);
    while(curr && curr->val < v)
    {
        pthread_rwlock_unlock(&prev->lock);
        prev = curr; curr = curr->next;
        if(curr) pthread_rwlock_rdlock(&curr->lock);
    }
    int ret = (curr && curr->val == v) ? 1 : 0;
    if(curr) pthread_rwlock_unlock(&curr->lock);
    pthread_rwlock_unlock(&prev->lock);
    return ret;
}

void *fn4(void *arg)
{
    int id = *(int*)arg, dummy = 0;
    for(int i = (num_ops/num_threads)*id; i < (num_ops/num_threads)*(id+1); i++)
    {
        const op_t& op = ops[i];
        if(op.op == OP_INSERT)      dummy += insert4(op.u, op.v);
        else if(op.op == OP_DELETE) dummy += delete4(op.u, op.v);
        else                        dummy += search4(op.u, op.v);
    }
    return NULL;
}

// P5: per-node CAS spin lock, hand-over-hand (lock=0 free, lock=1 held)
struct Node5 { int val; int lock; Node5 *next; };
vector<Node5*> adj5;

void init_adj5()
{
    adj5.resize(N);
    for(int i = 0; i < N; i++)
        adj5[i] = new Node5{INT_MIN, 0, NULL};
}

void seq_insert5(int u, int v)
{
    Node5 *prev = adj5[u], *curr = prev->next;
    while(curr && curr->val < v) { prev = curr; curr = curr->next; }
    if(curr && curr->val == v) return;
    prev->next = new Node5{v, 0, curr};
}

int insert5(int u, int v)
{
    Node5 *prev = adj5[u], *curr;
    acquire(&prev->lock);
    curr = prev->next;
    if(curr) acquire(&curr->lock);
    while(curr && curr->val < v)
    {
        release(&prev->lock);
        prev = curr; curr = curr->next;
        if(curr) acquire(&curr->lock);
    }
    if(curr && curr->val == v)
    {
        release(&curr->lock); release(&prev->lock);
        return 0;
    }
    prev->next = new Node5{v, 0, curr};
    if(curr) release(&curr->lock);
    release(&prev->lock);
    return 1;
}

int delete5(int u, int v)
{
    Node5 *prev = adj5[u], *curr;
    acquire(&prev->lock);
    curr = prev->next;
    if(curr) acquire(&curr->lock);
    while(curr && curr->val < v)
    {
        release(&prev->lock);
        prev = curr; curr = curr->next;
        if(curr) acquire(&curr->lock);
    }
    if(curr && curr->val == v)
    {
        prev->next = curr->next;
        release(&curr->lock);
        delete curr;
        release(&prev->lock);
        return 1;
    }
    if(curr) release(&curr->lock);
    release(&prev->lock);
    return 0;
}

int search5(int u, int v)
{
    Node5 *prev = adj5[u], *curr;
    acquire(&prev->lock);
    curr = prev->next;
    if(curr) acquire(&curr->lock);
    while(curr && curr->val < v)
    {
        release(&prev->lock);
        prev = curr; curr = curr->next;
        if(curr) acquire(&curr->lock);
    }
    int ret = (curr && curr->val == v) ? 1 : 0;
    if(curr) release(&curr->lock);
    release(&prev->lock);
    return ret;
}

void *fn5(void *arg)
{
    int id = *(int*)arg, dummy = 0;
    for(int i = (num_ops/num_threads)*id; i < (num_ops/num_threads)*(id+1); i++)
    {
        const op_t& op = ops[i];
        if(op.op == OP_INSERT)      dummy += insert5(op.u, op.v);
        else if(op.op == OP_DELETE) dummy += delete5(op.u, op.v);
        else                        dummy += search5(op.u, op.v);
    }
    return NULL;
}

// P6: optimistic CAS — traverse freely, lock+validate at commit, retry on failure
// search6 is lock-free; delete6 marks deleted before unlinking (no free: search may hold pointer)
struct Node6 { int val; int lock; int deleted; Node6 *next; };
vector<Node6*> adj6;

void init_adj6()
{
    adj6.resize(N);
    for(int i = 0; i < N; i++)
        adj6[i] = new Node6{INT_MIN, 0, 0, NULL};
}

void seq_insert6(int u, int v)
{
    Node6 *prev = adj6[u], *curr = prev->next;
    while(curr && curr->val < v) { prev = curr; curr = curr->next; }
    if(curr && curr->val == v) return;
    prev->next = new Node6{v, 0, 0, curr};
}

int search6(int u, int v)
{
    Node6 *curr = adj6[u]->next;
    while(curr && curr->val < v) curr = curr->next;
    return (curr && curr->val == v && !curr->deleted) ? 1 : 0;
}

int insert6(int u, int v)
{
    Node6 *prev, *curr;
    while(1)
    {
        prev = adj6[u]; curr = prev->next;
        while(curr && curr->val < v) { prev = curr; curr = curr->next; }
        acquire(&prev->lock);
        if(curr) acquire(&curr->lock);
        if(!prev->deleted && (!curr || !curr->deleted) && prev->next == curr)
        {
            if(curr && curr->val == v)
            {
                if(curr) release(&curr->lock);
                release(&prev->lock);
                return 0;
            }
            prev->next = new Node6{v, 0, 0, curr};
            if(curr) release(&curr->lock);
            release(&prev->lock);
            return 1;
        }
        if(curr) release(&curr->lock);
        release(&prev->lock);  // validation failed, retry
    }
}

int delete6(int u, int v)
{
    Node6 *prev, *curr;
    while(1)
    {
        prev = adj6[u]; curr = prev->next;
        while(curr && curr->val < v) { prev = curr; curr = curr->next; }
        acquire(&prev->lock);
        if(curr) acquire(&curr->lock);
        if(!prev->deleted && (!curr || !curr->deleted) && prev->next == curr)
        {
            if(curr && curr->val == v)
            {
                curr->deleted = 1;
                prev->next = curr->next;
                release(&curr->lock);
                release(&prev->lock);
                return 1;
            }
            if(curr) release(&curr->lock);
            release(&prev->lock);
            return 0;
        }
        if(curr) release(&curr->lock);
        release(&prev->lock);  // validation failed, retry
    }
}

void *fn6(void *arg)
{
    int id = *(int*)arg, dummy = 0;
    for(int i = (num_ops/num_threads)*id; i < (num_ops/num_threads)*(id+1); i++)
    {
        const op_t& op = ops[i];
        if(op.op == OP_INSERT)      dummy += insert6(op.u, op.v);
        else if(op.op == OP_DELETE) dummy += delete6(op.u, op.v);
        else                        dummy += search6(op.u, op.v);
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    if(argc < 5)
    {
        cerr << "Usage: " << argv[0] << " <graph_file> <ops_file> <num_ops> <num_threads>" << endl;
        return 1;
    }

    string graph_file = argv[1];
    string ops_file = argv[2];
    num_ops = atoi(argv[3]);
    num_threads = atoi(argv[4]);

    // read graph
    ifstream gin(graph_file);
    if(!gin.is_open()) { cerr << "Cannot open graph file: " << argv[1] << endl; return 1; }
    int M;
    gin >> N >> M;

    adj1.resize(N, NULL);
    init_adj2(); init_adj3(); init_adj4(); init_adj5(); init_adj6();

    for(int i = 0; i < M; i++)
    {
        int u, v; gin >> u >> v;
        seq_insert1(u, v); seq_insert2(u, v); seq_insert3(u, v);
        seq_insert4(u, v); seq_insert5(u, v); seq_insert6(u, v);
    }
    gin.close();

    // read operations
    ifstream oin(ops_file);
    if(!oin.is_open()) { cerr << "Cannot open ops file: " << argv[2] << endl; return 1; }
    ops.resize(num_ops);
    for(int i = 0; i < num_ops; i++)
    {
        string s; int u, v;
        oin >> s >> u >> v;
        if(s == "insert")      ops[i].op = OP_INSERT;
        else if(s == "delete") ops[i].op = OP_DELETE;
        else                   ops[i].op = OP_SEARCH;
        ops[i].u = u; ops[i].v = v;
    }
    oin.close();

    struct { void*(*fn)(void*); const char *label; } programs[] = {
        { fn1, "P1 (global mutex)   " },
        { fn2, "P2 (per-list mutex) " },
        { fn3, "P3 (per-node mutex) " },
        { fn4, "P4 (per-node rwlock)" },
        { fn5, "P5 (per-node CAS)   " },
        { fn6, "P6 (optimistic CAS) " },
    };

    for(auto &p : programs)
    {
        long elapsed = run_threads(p.fn);
        printf("%s: Time: %ld us, Throughput: %.2f ops/sec\n",
               p.label, elapsed, (double)num_ops * 1e6 / elapsed);
    }

    return 0;
}
