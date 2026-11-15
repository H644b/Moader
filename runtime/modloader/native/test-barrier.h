/* Host-test portability for platforms without POSIX barriers. */
#if defined(__APPLE__)
typedef struct {
 pthread_mutex_t mutex;
 pthread_cond_t condition;
 unsigned count,waiting,generation;
} pthread_barrier_t;
static int pthread_barrier_init(pthread_barrier_t *b,const void *attr,unsigned count){
 (void)attr;memset(b,0,sizeof(*b));b->count=count;
 int rc=pthread_mutex_init(&b->mutex,0);if(rc)return rc;
 rc=pthread_cond_init(&b->condition,0);if(rc)pthread_mutex_destroy(&b->mutex);return rc;
}
static int pthread_barrier_wait(pthread_barrier_t *b){
 pthread_mutex_lock(&b->mutex);unsigned generation=b->generation;
 if(++b->waiting==b->count){b->waiting=0;b->generation++;pthread_cond_broadcast(&b->condition);}
 else while(generation==b->generation)pthread_cond_wait(&b->condition,&b->mutex);
 pthread_mutex_unlock(&b->mutex);return 0;
}
static int pthread_barrier_destroy(pthread_barrier_t *b){
 int rc=pthread_cond_destroy(&b->condition);int other=pthread_mutex_destroy(&b->mutex);return rc?rc:other;
}
#endif
