/* Program for project marking problem, COMSM2001 Assignment 1 */

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>

int S;   /* Number of students */
int M;   /* Number of markers */
int K;   /* Number of markers per demo */
int N;   /* Number of demos per marker */
int T;   /* Length of session (minutes) */
int D;   /* Length of demo (minutes) */
         /* S*K <= M*N */
         /* D <= T */

int *student_list; /* list of studentID */
int *marker_need; /* list of numbers of markers that each student needs */
int *student_finished; /* list of statements whether student finished demo */
int in_position = 0; /* position nubmer when a studentID is added to the list */
int out_position = 0; /* position number for getting student */
int timeout = 0; /* the end of session approaches */

pthread_mutex_t m;

pthread_cond_t student_grab;
pthread_cond_t student_ready;
pthread_cond_t student_finish;

struct timeval starttime;

/* timenow(): returns current simulated time in "minutes" (cs) */
int timenow()
{
  struct timeval now;
  gettimeofday(&now, NULL);
  return (now.tv_sec-starttime.tv_sec)*100 + (now.tv_usec-starttime.tv_usec)/10000;
}

/* delay(t): delays for t "minutes" (cs) */
void delay(int t)
{
  struct timespec rqtp, rmtp;
  t *= 10;
  rqtp.tv_sec = t / 1000;
  rqtp.tv_nsec = 1000000 * (t % 1000);
  nanosleep(&rqtp, &rmtp);
}

/* panic(): simulates a student's panicking activity */
void panic()
{
  delay(random()%(T-D));
}

/* demo(): simulates a demo activity */
void demo()
{
  delay(D);
}

/* marker(arg): marker thread */
void *marker(void *arg)
{
  int markerID = *(int *)arg;
  int job, studentID;
  printf("%d marker %d: enters lab\n", timenow(), markerID);
  for (job=0; job<N; job++) {
    /* wait for to be grabbed by a student */
    pthread_mutex_lock(&m);
    while(in_position == out_position && !timeout){
      pthread_cond_wait(&student_grab, &m);
    }
    /* get the studentID from the list */
    studentID = student_list[out_position];
    marker_need[studentID]--;
    /* if student has enough markers, move to the next position */
    if(marker_need[studentID] == 0){
      out_position++;
      out_position = out_position % S;
    }
    pthread_mutex_unlock(&m);

    if (timeout) {
      break;
    }

    pthread_mutex_lock(&m);
    printf("%d marker %d: grabbed by student %d (job %d)\n", timenow(), markerID, studentID, job+1);
    /* If the student has enough(K) markers, wake up student to demo */
    if(marker_need[studentID] == 0){
      pthread_cond_broadcast(&student_ready);
    }
    pthread_mutex_unlock(&m);
    /* wait for student's demo to finish */
    pthread_mutex_lock(&m);
    while(student_finished[studentID] != 1) {
      pthread_cond_wait(&student_finish, &m);
    }
    pthread_mutex_unlock(&m);

    if(timeout) {
      break;
    }

    printf("%d marker %d: finished with student %d (job %d)\n", timenow(), markerID, studentID, job+1);
  }
  if (job == N) {
    printf("%d marker %d: exits lab (finished %d jobs)\n", timenow(), markerID, N);
  }
  else {
    printf("%d marker %d: exits lab (timeout)\n", timenow(), markerID);
  }
  return NULL;
}

/* student(arg): student thread */
void *student(void *arg)
{
  int studentID = *(int *)arg;
  printf("%d student %d: starts panicking\n", timenow(), studentID);
  panic();
  printf("%d student %d: enters lab\n", timenow(), studentID);
  /* put student in the list */
  pthread_mutex_lock(&m);
  student_list[in_position] = studentID;
  in_position++;
  in_position = in_position % S;
  pthread_mutex_unlock(&m);
  /* wake up markers */
  pthread_mutex_lock(&m);
  pthread_cond_broadcast(&student_grab);
  pthread_mutex_unlock(&m);
  /* wait for enough markers */
  pthread_mutex_lock(&m);
  while(marker_need[studentID] != 0 && !timeout){
    pthread_cond_wait(&student_ready, &m);
  }
  pthread_mutex_unlock(&m);

  if (timeout) {
    pthread_mutex_lock(&m);
    printf("%d student %d: exits lab (timeout)\n", timenow(), studentID);
    student_finished[studentID] = 1;
    pthread_cond_broadcast(&student_finish);
    pthread_mutex_unlock(&m);
  }
  else {
    /* demo starts */
    printf("%d student %d: starts demo\n", timenow(), studentID);
    demo();
    printf("%d student %d: ends demo\n", timenow(), studentID);

    pthread_mutex_lock(&m);
    student_finished[studentID] = 1;
    pthread_cond_broadcast(&student_finish);
    pthread_mutex_unlock(&m);

    printf("%d student %d: exits lab (finished)\n", timenow(), studentID);
  }
  return NULL;
}
/* initialisation */
void initialise(){
  int i;
  pthread_mutex_init(&m, NULL);
  pthread_cond_init(&student_grab, NULL); 
  pthread_cond_init(&student_ready, NULL);
  pthread_cond_init(&student_finish, NULL);

  student_list = malloc(sizeof(int) * S);
  marker_need =  malloc(sizeof(int) * S);
  student_finished =  malloc(sizeof(int) * S);

  for(i=0; i<S; i++) {
    student_list[i] = -1;
    marker_need[i] = K;
    student_finished[i] = 0;
  }
}


int main(int argc, char *argv[])
{
  int i;
  int markerID[100], studentID[100];
  pthread_t markerT[100], studentT[100];

  if (argc < 6) {
    printf("Usage: demo S M K N T D\n");
    exit(1);
  }
  S = atoi(argv[1]);
  M = atoi(argv[2]);
  K = atoi(argv[3]);
  N = atoi(argv[4]);
  T = atoi(argv[5]);
  D = atoi(argv[6]);
  if (M > 100 || S > 100) {
    printf("Maximum 100 markers and 100 students allowed\n");
    exit(1);
  }

  printf("S=%d M=%d K=%d N=%d T=%d D=%d\n", S, M, K, N, T, D);
  gettimeofday(&starttime, NULL);  /* Save start of simulated time */

  initialise();

  /* Create S student threads */
    for (i=0; i<S; i++) {
    studentID[i] = i;
    pthread_create(&studentT[i], NULL, student, &studentID[i]);
  }
  /* Create M marker threads */
  for (i=0; i<M; i++) {
    markerID[i] = i;
    pthread_create(&markerT[i], NULL, marker, &markerID[i]);
  }
  delay(T-D);   /* Wait until latest time demo can start */
  /* timeout, notifiy all threads */
  timeout = 1;
  pthread_cond_broadcast(&student_grab);
  pthread_cond_broadcast(&student_ready);
  pthread_cond_broadcast(&student_finish);
  /* Wait for student threads to finish */
  for (i=0; i<S; i++) {
    pthread_join(studentT[i], NULL);
  }
  /* Wait for marker threads to finish */
  for (i=0; i<M; i++) {
    pthread_join(markerT[i], NULL);
  }
  return 0;
}
