#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>

#define CLOCKID CLOCK_REALTIME
#define NUM_THREADS 3
#define errExit(msg)    do { perror(msg); exit(EXIT_FAILURE); \
                        } while (0)

typedef struct
{
    pthread_t threadID;
    int policy;
    int threadNUM;
    struct sched_param param;
    struct sigevent mySignalEvent;
    struct itimerspec timerSpec;
    int signal_number; //signal value
    sigset_t timer_signal;
    int missed_signal_count;
    timer_t timer_Id;
    long long timer_Period;
    struct timeval tm1;
    struct timeval tm2;
    unsigned long long wait[10];
    long jitter[10];

}ThreadInfo;

void* thread_func(void *arg);
void CreateAndArmTimer(int unsigned period, ThreadInfo* threadInfo);
void WaitForTimer(ThreadInfo* threadInfo);
void prsigset(FILE *, sigset_t *);

ThreadInfo myThreadInfo[NUM_THREADS];
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

int main(void) {

  int fifoPri = 60;
  sigset_t alarm_sig; //data structure that holds signals
  sigemptyset (&alarm_sig); //initializes signal set to exlude all defined signals

  for (int i = SIGRTMIN; i <= SIGRTMAX; i++)
    sigaddset (&alarm_sig, i); //adds all signals available to the set

  sigprocmask (SIG_BLOCK, &alarm_sig, NULL); //BLOCKS ALL signals in set

  //creates threads
  for(int i = 0; i<NUM_THREADS; i++) {

    myThreadInfo[i].threadNUM = i+1;
    myThreadInfo[i].policy = SCHED_FIFO;
    myThreadInfo[i].param.sched_priority = fifoPri++;
    pthread_create( &myThreadInfo[i].threadID, NULL, thread_func, &myThreadInfo[i]);

  }


  for (int k = 0; k < NUM_THREADS; k++) {

      if (pthread_join(myThreadInfo[k].threadID, NULL) != 0) {
        perror("pthread_join() error");
        exit(5);
      }

  }

  return 0;
}

void* thread_func(void *arg)
{

  ThreadInfo* currentThread;
  currentThread = (ThreadInfo*) arg;
  pthread_t id = pthread_self();
  time_t T;

  if(currentThread->threadNUM == 1)
    currentThread->timer_Period = 1000000;

  if(currentThread->threadNUM == 2)
    currentThread->timer_Period = 2000000;

  if(currentThread->threadNUM == 3)
    currentThread->timer_Period = 4000000;


  if(pthread_setschedparam(pthread_self(), currentThread->policy, (const struct sched_param *) &(currentThread->param))){
     perror("pthread_setschedparm failed");
     pthread_exit(NULL);
  }

  if(pthread_getschedparam(pthread_self(), &currentThread->policy, (struct sched_param *) &currentThread->param)){
     perror("pthread_getschedparam failed");
     pthread_exit(NULL);
  }

  CreateAndArmTimer(currentThread->timer_Period, currentThread);

  for (int i = 0; i < 5; i++) {

    time(&T);
    gettimeofday(&currentThread->tm1, NULL);

    WaitForTimer(currentThread);

    time(&T);
    gettimeofday(&currentThread->tm2, NULL);

    //calculate time in WaitForTimer
    currentThread->wait[i] = 1000000 * (currentThread->tm2.tv_sec - currentThread->tm1.tv_sec) + (currentThread->tm2.tv_usec - currentThread->tm1.tv_usec);
    //jitter 
    currentThread->jitter[i] = currentThread->wait[i] - currentThread->timer_Period;

    printf("Thread[%d]    Timer Delta [%lld]us    Jitter[%ld]us\n", currentThread->threadNUM, currentThread->wait[i], currentThread->jitter[i]);


  }

  pthread_exit(NULL);
}

void CreateAndArmTimer(int unsigned period, ThreadInfo* threadInfo)
{
  //Create a static int variable to keep track of the next available signal number
  static int currentSig;
  long seconds;
  long nanoseconds;
  int ret;

  if (pthread_mutex_lock(&mutex) != 0) {
    perror("pthread_mutex_lock() error");
    exit(6);
  }

  if (currentSig == 0)
    currentSig = SIGRTMIN;

  threadInfo->signal_number = currentSig;
  currentSig++;

  pthread_mutex_unlock(&mutex);

  sigemptyset(&threadInfo->timer_signal);
  sigaddset(&threadInfo->timer_signal, threadInfo->signal_number);

  threadInfo->mySignalEvent.sigev_notify = SIGEV_SIGNAL;
  threadInfo->mySignalEvent.sigev_signo = threadInfo->signal_number; //A value b/t SIGRTMIN and SIGRTMAX
  threadInfo->mySignalEvent.sigev_value.sival_ptr = (void *)&(threadInfo->timer_Id);
  ret = timer_create (CLOCK_MONOTONIC, &threadInfo->mySignalEvent, &threadInfo->timer_Id);
  if (ret == -1)
      errExit("timer_create");

  seconds = period/1000000;
  nanoseconds = (period - (seconds * 1000000)) * 1000;
  threadInfo->timerSpec.it_interval.tv_sec = seconds;
  threadInfo->timerSpec.it_interval.tv_nsec = nanoseconds;
  threadInfo->timerSpec.it_value.tv_sec = seconds;
  threadInfo->timerSpec.it_value.tv_nsec = nanoseconds;
  ret = timer_settime (threadInfo->timer_Id, 0, &threadInfo->timerSpec, NULL);
  if (ret == -1)
     errExit("timer_settime");

}

void WaitForTimer(ThreadInfo* threadInfo)
{
  if(sigwait(&threadInfo-> timer_signal, &threadInfo->signal_number) != 0)
    errExit("sigwait");

  threadInfo-> missed_signal_count = timer_getoverrun(threadInfo->timer_Id);

}
