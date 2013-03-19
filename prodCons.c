/*************************************************************************/
/*  prodCons.c                                                           */
/*                                                                       */
/*************************************************************************/

/* includes */
#include "vxWorks.h"
#include "stdio.h"
#include "stdlib.h"
#include "semLib.h"
#include "taskLib.h"
#include "kernelLib.h"
#include "tickLib.h"
#include "time.h"
#include "sigLib.h"
#include "errno.h"

/* defines */
#define STACK_SIZE    20000
#define MAX_SECONDS   100
#define MAX_DEPTH     1000
#define MAX_PERIOD    100
#define MAX_BOUND     100
#define MIN_BOUND     10
#define MAX_COMP_TIME 100
#define MIN_MSG       2
#define MAX_MSG       100
#define TIMESLICE     6 // set time slice to 100 ms
#define MAX_MSG_LEN   34

#define TYPE_PERIODIC  'P'
#define TYPE_APERIODIC 'A'
#define STR_PERIODIC   "PERIODIC"
#define STR_APERIODIC  "APERIODIC"

#define IDENT "                               "
#define true  1
#define false 0

/* task IDs */
int tidProdPeriodic;			
int tidProdAperiodic;			
int tidConsumer;

/* queue IDs */
MSG_Q_ID qidPeriodic;
MSG_Q_ID qidAperiodic;

/* function declarations */
void prodPeriodic(int);
void prodAperiodic(int, int);
void consumer(int, int);
void timerHandlerPeriodic(timer_t, int*);
void timerHandlerAperiodic(timer_t, int*);

typedef int bool;


/*************************************************************************/
/*  main task                                                            */
/*                                                                       */
/*************************************************************************/

int main(void) {
    struct timespec mytime;
    int    nseconds = 0;
    int    depth_q1 = 0;
    int    depth_q2 = 0;
    int    period = 0;
    int    low_bound = 0;
    int    up_bound = 0;
    int    comp_time = 0;
    int    max_read_msg = 0;

    /* get the simulation time */ 
    printf("\n\n");
    while ((nseconds < 1) || (nseconds > MAX_SECONDS)) {
        printf("Enter overall simulation time [1-%d s]: ", MAX_SECONDS);
        scanf("%d", &nseconds);
    };
    printf("Simulating for %d seconds.\n\n", nseconds);

    /* get the maximal queue entries for queue #1*/ 
    while ((depth_q1 < 1) || (depth_q1 > MAX_DEPTH)) {
        printf("Enter depth of periodic queue [1-%d]: ", MAX_DEPTH);
        scanf("%d", &depth_q1);
    };
    printf("Depth of periodic queue set to %d entries.\n\n", depth_q1);

    /* get the maximal queue entries for queue #2*/ 
    while ((depth_q2 < 1) || (depth_q2 > MAX_DEPTH)) {
        printf("Enter depth of aperiodic queue [1-%d]: ", MAX_DEPTH);
        scanf("%d", &depth_q2);
    };
    printf("Depth of aperiodic queue set to %d entries.\n\n", depth_q2);

    /* get the period for the periodic producer */ 
    while ((period < 1) || (period > MAX_PERIOD)) {
        printf("Enter period for periodic producer [1-%d s]: ", MAX_PERIOD);
        scanf("%d", &period);
    };
    printf("Period for periodic producer set to %ds. \n\n", period);

    /* get the lower bound for the aperiodic timer */ 
    while ((low_bound < 1) || (low_bound > MAX_BOUND)) {
        printf("Enter lower bound for aperiodic timer [1-%d s]: ", MAX_BOUND);
        scanf("%d", &low_bound);
    };
    printf("Lower bound for aperiodic timer set to %ds. \n\n", low_bound);

    /* get the upper bound for the aperiodic timer */ 
    while ((up_bound < MIN_BOUND) || (up_bound > MAX_BOUND)) {
        printf("Enter upper bound for aperiodic timer [%d-%d s]: ", MIN_BOUND, MAX_BOUND);
        scanf("%d", &up_bound);
    };
    printf("Upper bound for aperiodic timer set to %ds. \n\n", up_bound);

    /* get the consumer computation time */ 
    while ((comp_time < 1) || (comp_time > MAX_COMP_TIME)) {
        printf("Enter consumer computation time [1-%d s]: ", MAX_COMP_TIME);
        scanf("%d", &comp_time);
    };
    printf("Consumer computation time set to %ds. \n\n", comp_time);

    /* get the max number of msgs read per consumer loop */ 
    while ((max_read_msg < MIN_MSG) || (max_read_msg > MAX_MSG)) {
        printf("Enter max number of messages read per consumer loop [%d-%d]: ", MIN_MSG, MAX_MSG);
        scanf("%d", &max_read_msg);
    };
    printf("Max number of messages read per consumer loop set to %d. \n\n", max_read_msg);


    /* set clock to start at 0 */
    mytime.tv_sec  = 0;
    mytime.tv_nsec = 0;

    if (clock_settime(CLOCK_REALTIME, &mytime) < 0)
        printf("Error clock_settime\n");
    else
        printf("Current time set to %d sec %d ns \n\n",
                (int) mytime.tv_sec, (int)mytime.tv_nsec);

    /* set time slice to 100 ms */	
    kernelTimeSlice(TIMESLICE); 	
     
    /* spawn (create and start) task */
    tidProdPeriodic = taskSpawn("tProdPeriodic", 100, 0, STACK_SIZE,
        (FUNCPTR)prodPeriodic, period, 0, 0, 0, 0, 0, 0, 0, 0, 0);

    /* spawn (create and start) task */
    tidProdAperiodic = taskSpawn("tProdAperiodic", 100, 0, STACK_SIZE,
        (FUNCPTR)prodAperiodic, low_bound, up_bound, 0, 0, 0, 0, 0, 0, 0, 0);

    /* spawn (create and start) task */
    tidConsumer = taskSpawn("tConsumer", 100, 0, STACK_SIZE,
        (FUNCPTR)consumer, comp_time, max_read_msg, 0, 0, 0, 0, 0, 0, 0, 0);

    /* run for given simulation time */
    taskDelay(nseconds*60);

    /* delete task */
    taskDelete(tidProdPeriodic);
    taskDelete(tidProdAperiodic);
    taskDelete(tidConsumer);

    printf("Exiting. \n\n");
    return(0);
}


/*************************************************************************/
/*  task "tProdPeriodic"                                                 */
/*                                                                       */
/*************************************************************************/

void prodPeriodic(int period) {
	int i;
	timer_t ptimer;
	struct itimerspec intervaltimer;
    int msgCnt = 0;
        
    /* create message queue */
    if ((qidPeriodic = msgQCreate (MAX_DEPTH, MAX_MSG_LEN, MSG_Q_PRIORITY)) == NULL)
		printf("Error msgQCreate\n");
	else
		printf("Queue for periodic producer created.\n");

	/* create timer */
	if ( timer_create(CLOCK_REALTIME, NULL, &ptimer) == ERROR)
		printf("Error create_timer\n");
	else
		printf("Timer for periodic producer created.\n");

	/* connect timer to timer handler routine */
	if ( timer_connect(ptimer, (VOIDFUNCPTR)timerHandlerPeriodic, (int)&msgCnt) == ERROR )
		printf("Error connect_timer\n");
	else
		printf("Timer handler for periodic producer connected.\n");

	/* set and arm timer */
	intervaltimer.it_value.tv_sec = period;
	intervaltimer.it_value.tv_nsec = 0;
	intervaltimer.it_interval.tv_sec = period;
	intervaltimer.it_interval.tv_nsec = 0;

	if ( timer_settime(ptimer, TIMER_ABSTIME, &intervaltimer, NULL) == ERROR )
		printf("Error set_timer\n");
	else
		printf("Timer for periodic producer set to %ds.\n\n",
                intervaltimer.it_interval.tv_sec);

	/* idle loop */
	while(1) pause();
}


/*************************************************************************/
/*  task "tProdAeriodic"                                                 */
/*                                                                       */
/*************************************************************************/

void prodAperiodic(int low_bound, int up_bound) {
    int i;
    int period;
    timer_t ptimer;
    struct  itimerspec intervaltimer;
    int msgCnt = 0;

    /* create message queue */
    if ((qidAperiodic = msgQCreate (MAX_DEPTH, MAX_MSG_LEN, MSG_Q_PRIORITY)) == NULL)
		printf("Error msgQCreate\n");
	else
		printf("Queue for aperiodic producer created.\n");

    /* create timer */
    if ( timer_create(CLOCK_REALTIME, NULL, &ptimer) == ERROR)
        printf("Error create_timer\n");
    else
        printf("Timer for aperiodic producer created.\n");

    /* connect timer to timer handler routine */
    if ( timer_connect(ptimer, (VOIDFUNCPTR)timerHandlerAperiodic, (int)&msgCnt) == ERROR )
        printf("Error connect_timer\n");
    else
        printf("Timer handler for aperiodic producer connected.\n");

    /* generate random period */
    period = random_in_range(low_bound, up_bound+1);

    /* set and arm timer */
    intervaltimer.it_value.tv_sec = period;
    intervaltimer.it_value.tv_nsec = 0;
    intervaltimer.it_interval.tv_sec = period;
    intervaltimer.it_interval.tv_nsec = 0;

    if ( timer_settime(ptimer, TIMER_ABSTIME, &intervaltimer, NULL) == ERROR )
        printf("Error set_timer\n");
    else
        printf("Timer for aperiodic producer set to %ds.\n\n",
                intervaltimer.it_interval.tv_sec);

    /* idle loop */
    while(1) pause();
}


/*************************************************************************/
/*  task "tConsumer"                                                     */
/*                                                                       */
/*************************************************************************/

void consumer(int comp_time, int max_read_msg) {
    char msgBuf[MAX_MSG_LEN];
    int byteCnt, i, zeroCnt;
    MSG_Q_ID qid;
    bool periodic = true;
    struct timespec mytime;
    char* src;

    while (1) {
        taskDelay(comp_time*60);
        qid = (periodic) ? qidPeriodic : qidAperiodic;
        zeroCnt = 0;
        for (i=0; i<max_read_msg; i++) {
            /* get message from queue */
            if (msgQReceive(qid, msgBuf, MAX_MSG_LEN, NO_WAIT) == ERROR) {
                if (errno == S_objLib_OBJ_UNAVAILABLE) {
                    // printf("Queue empty\n");
                    zeroCnt++;
                    if (zeroCnt >= 2)
                        break; // both queues are empty

                    // one queue is empty, switch to the other
                    qid = (periodic) ? qidAperiodic : qidPeriodic;
                }
                else {
                    printf("Error msgQReceive\n");
                }
            }
            else {
                if (msgBuf[0] == TYPE_PERIODIC)
                    src = STR_PERIODIC;
                else if (msgBuf[0] == TYPE_APERIODIC)
                    src = STR_APERIODIC;
                else
                    printf("Error: unknown source\n");

                if ( clock_gettime (CLOCK_REALTIME, &mytime) == ERROR) 
                    printf("Error: clock_gettime \n");

                printf(IDENT"CONSUMER: message #%03s from %s @ %03ds.\n",
                        msgBuf+1, src, (int)mytime.tv_sec);
            }
        }
        periodic = (periodic) ? false : true; //periodic = !periodic;
    };
}


/*************************************************************************/
/*  function "TimerHandlerPeriodic"                                      */
/*                                                                       */
/*************************************************************************/

void timerHandlerPeriodic(timer_t callingtimer, int* msgCnt) {
    struct timespec mytime;
    char msgId[MAX_MSG_LEN-1];
    char msg[MAX_MSG_LEN];
    // printf("periodic: set msgId\n");
    sprintf(msgId, "%d", *msgCnt);
    // printf("periodic: set msg\n");
    sprintf(msg, "%c%d", TYPE_PERIODIC, *msgCnt);
    (*msgCnt)++;

    // printf("periodic: set time\n");
    if ( clock_gettime (CLOCK_REALTIME, &mytime) == ERROR) 
        printf("Error: clock_gettime \n");

    // printf("periodic: send msg\n");
    /* send a normal priority message, blocking if queue is full */
    if (msgQSend (qidPeriodic, msg, sizeof(msg), WAIT_FOREVER,
                MSG_PRI_NORMAL) == ERROR)
        printf("Error: msgQSend\n");

    printf(STR_PERIODIC": message #%03s @ %03ds.\n", msgId, (int)mytime.tv_sec);
}


/*************************************************************************/
/*  function "TimerHandlerAperiodic"                                     */
/*                                                                       */
/*************************************************************************/

void timerHandlerAperiodic(timer_t callingtimer, int* msgCnt) {
    struct timespec mytime;
    char msgId[MAX_MSG_LEN-1];
    char msg[MAX_MSG_LEN];
    // printf("aperiodic: set msgId\n");
    sprintf(msgId, "%d", *msgCnt);
    // printf("aperiodic: set msg\n");
    sprintf(msg, "%c%d", TYPE_APERIODIC, *msgCnt);
    (*msgCnt)++;

    // printf("aperiodic: set time\n");
    if ( clock_gettime (CLOCK_REALTIME, &mytime) == ERROR) 
        printf("Error: clock_gettime \n");

    // printf("aperiodic: send msg\n");
    /* send a normal priority message, blocking if queue is full */
    if (msgQSend (qidAperiodic, msg, sizeof(msg), WAIT_FOREVER,
                MSG_PRI_NORMAL) == ERROR)
        printf("Error: msgQSend\n");

    printf(STR_APERIODIC": message #%03s @ %03ds.\n", msgId, (int)mytime.tv_sec);
}
  
/*************************************************************************/
/*  function "random_in_range"                                           */
/*                                                                       */
/*  proposed by Ryan Reich on http://stackoverflow.com                   */
/*                                                                       */
/*************************************************************************/

/* Would like a semi-open interval [min, max) */
int random_in_range (unsigned int min, unsigned int max)
{
    int base_random = rand(); /* in [0, RAND_MAX] */
    if (RAND_MAX == base_random) return random_in_range(min, max);
    /* now guaranteed to be in [0, RAND_MAX) */
    int range = max - min,
    remainder = RAND_MAX % range,
    bucket    = RAND_MAX / range;
    /* There are range buckets, plus one smaller interval
    *      within remainder of RAND_MAX */
    if (base_random < RAND_MAX - remainder) {
        return min + base_random/bucket;
    }
    else {
        return random_in_range (min, max);
    }
}
