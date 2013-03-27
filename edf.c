/*************************************************************************/
/*  edf.c                                                                */
/*                                                                       */
/*  Simon Maurer                                                         */
/*  2x.03.2013                                                           */
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
#define MAX_SECONDS   1000
#define MAX_PERIODIC  3
#define MAX_APERIODIC 3
#define MAX_PENDING   (MAX_PERIODIC)
#define MAX_READY     (MAX_PERIODIC)
#define MAX_PERIOD    100
#define MAX_DEADLINE  100
#define MAX_PRIO      103
#define MIN_PRIO      255

#define ET_ARRIVE   0
#define ET_DEADLINE 1

#define ST_RUN      0
#define ST_SUSPEND  1

#define Q_TE_MSG_SIZE sizeof(q_te_param)

#define LOG_INFO    0
#define LOG_WARNING 1
#define LOG_ERROR   2
#define LOG_DEBUG   3

typedef int bool;
#define true  1
#define false 0

typedef struct task_param {
    int period;
    int exec_time;
    int id;
} t_param;

typedef struct task_pending_param {
    bool is_set;
    bool is_periodic;
    struct timespec qt;
} tp_param;

typedef struct time_event_param {
    t_param* t_params;
    tp_param tp_params[MAX_PENDING];
} te_param;

typedef struct queue_time_event_param {
    int event_type;
    int task_id;
    struct timespec dl;
} q_te_param;

/* task IDs */
int tidTimerMux;
int tidScheduler;

/* queue IDs */
MSG_Q_ID qidTimeEvents;

/* function declarations */
void timerMux(t_param*, int);
void timerHandler(timer_t, te_param*);
void scheduler();
void periodic(int);
void print_log_prefix(int);


/*************************************************************************/
/*  main task                                                            */
/*                                                                       */
/*************************************************************************/

int main(void) {
    struct  timespec mytime;
    int     task_cnt = 0;
    int     nseconds = 0;
    int     i;
    t_param t_params[MAX_PERIODIC];
    char t_name[20];

    /* get the simulation time */ 
    printf("\n\n");
    while ((nseconds < 1) || (nseconds > MAX_SECONDS)) {
        printf("Enter overall simulation time [1-%d s]: ", MAX_SECONDS);
        scanf("%d", &nseconds);
    };
    printf("Simulating for %d seconds.\n\n", nseconds);

    /* get the number of tasks */
    while ((task_cnt < 1) || (task_cnt > MAX_PERIODIC)) {
        printf("Enter the number of periodic tasks to be scheduled [1-%d]: ", MAX_PERIODIC);
        scanf("%d", &task_cnt);
    };
    printf("Number of periodic tasks set to %d.\n\n", task_cnt);

    for (i = 0; i < task_cnt; i++){
        // get period of task i
        while ((t_params[i].period < 1) || (t_params[i].period > MAX_PERIOD)) {
            printf("Enter the period of task %d [1-%d]s: ", i+1, MAX_PERIOD);
            scanf("%d", &t_params[i].period);
        };
        printf("Period of task %d set to %d.\n\n", i+1, t_params[i].period);

        // get execution time of task i
        while ((t_params[i].exec_time < 1) || (t_params[i].exec_time > t_params[i].period)) {
            printf("Enter the execution time of task %d [1-%d]s: ", i+1, t_params[i].period);
            scanf("%d", &t_params[i].exec_time);
        };
        printf("Execution time of task %d set to %d.\n\n", i+1, t_params[i].exec_time);
    }

    /* set clock to start at 0 */
    mytime.tv_sec  = 0;
    mytime.tv_nsec = 0;

    if (clock_settime(CLOCK_REALTIME, &mytime) < 0)
        printf("Error clock_settime\n");
    else
        printf("Current time set to %d sec %d ns \n\n",
                (int) mytime.tv_sec, (int)mytime.tv_nsec);

    /* spawn (create and start) timer task */
    tidTimerMux = taskSpawn("tTimerMux", 101, 0, STACK_SIZE,
        (FUNCPTR)timerMux, (int)t_params, task_cnt, 0, 0, 0, 0, 0, 0, 0, 0);

    /* create scheduler task */
    tidScheduler = taskCreate("tScheduler", 102, 0, STACK_SIZE,
        (FUNCPTR)scheduler, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

    /* create periodic tasks */
    for (i=0; i<task_cnt; i++) {
        sprintf(t_name, "tPeriodic_%d", i);
        t_params[i].id = taskCreate(t_name, 255, 0, STACK_SIZE,
            (FUNCPTR)periodic, t_params[i].exec_time, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        print_log_prefix(LOG_DEBUG);
        printf("main        | task created (id: %d)\n", t_params[i].id);
    }

    /* run for given simulation time */
    taskDelay(nseconds*sysClkRateGet());

    /* create periodic tasks */
    for (i=0; i<task_cnt; i++) {
        taskDelete(t_params[i].id);
    }
    taskDelete(tidTimerMux);

    printf("Exiting. \n\n");
    return(0);
}


/*************************************************************************/
/*  multiplexed timer task                                               */
/*                                                                       */
/*************************************************************************/

void timerMux(t_param* t_params, int task_cnt) {
    int i;
    timer_t ptimer;
    struct itimerspec intervaltimer;
    te_param te_params;

    /* create message queue */
    if ((qidTimeEvents = msgQCreate (MAX_PENDING*2, Q_TE_MSG_SIZE, MSG_Q_PRIORITY)) == NULL)
        printf("Error msgQCreate\n");

    /* create timer */
    if ( timer_create(CLOCK_REALTIME, NULL, &ptimer) == ERROR)
        printf("Error create_timer\n");

    /* initialize timing event parameters */
    te_params.t_params = t_params;
    for (i=0; i<MAX_PENDING; i++) {
        te_params.tp_params[i].is_set = (i < task_cnt) ? true : false;
        te_params.tp_params[i].is_periodic = true;
        te_params.tp_params[i].qt.tv_sec = 0;
        te_params.tp_params[i].qt.tv_nsec = 1;
    }

    /* connect timer to timer handler routine */
    if ( timer_connect(ptimer, (VOIDFUNCPTR)timerHandler, (int)&te_params) == ERROR )
        printf("Error connect_timer\n");

    /* set and arm timer */
    intervaltimer.it_value.tv_sec = 0;
    intervaltimer.it_value.tv_nsec = 1;
    intervaltimer.it_interval.tv_sec = 0;
    intervaltimer.it_interval.tv_nsec = 0;

    if ( timer_settime(ptimer, TIMER_ABSTIME, &intervaltimer, NULL) == ERROR )
        printf("Error set_timer\n");

    /* idle loop */
    while(1) pause();
}


/*************************************************************************/
/*  timer handler                                                        */
/*                                                                       */
/*************************************************************************/

void timerHandler(timer_t callingtimer, te_param* te_params) {
    int i, idx;
    struct itimerspec intervaltimer;
    q_te_param q_te_params;
    
    for (i=0; i<MAX_PENDING; i++) {
        if ((*te_params).tp_params[i].is_set) {
            /* send time events */
            if ((*te_params).tp_params[i].qt.tv_sec != 0) {
                /* add deadline time event to the queue */ 
                q_te_params.event_type = ET_DEADLINE;
                q_te_params.task_id = (*te_params).t_params[i].id;
                q_te_params.dl = (*te_params).tp_params[i].qt;
                if (msgQSend (qidTimeEvents, (char*)&q_te_params, Q_TE_MSG_SIZE, NO_WAIT, MSG_PRI_NORMAL) == ERROR) {
                    print_log_prefix(LOG_ERROR);
                    printf("timer       | cannot send queue dl msg (msgQSend)\n");
                }
                print_log_prefix(LOG_DEBUG);
                printf("timer       | msgQSend: %d, %d, %d, %d\n", q_te_params.task_id,
                    q_te_params.event_type, q_te_params.dl.tv_sec, q_te_params.dl.tv_nsec);
            }
            /* set new queue times */
            (*te_params).tp_params[i].qt.tv_sec += (*te_params).t_params[i].period;
            (*te_params).tp_params[i].is_set = false;
            /* add arrive time event to the queue */ 
            q_te_params.event_type = ET_ARRIVE;
            q_te_params.task_id = (*te_params).t_params[i].id;
            q_te_params.dl = (*te_params).tp_params[i].qt;
            if (msgQSend (qidTimeEvents, (char*)&q_te_params, Q_TE_MSG_SIZE, NO_WAIT, MSG_PRI_NORMAL) == ERROR) {
                print_log_prefix(LOG_ERROR);
                printf("timer       | cannot send queue msg at (msgQSend)\n");
            }
            print_log_prefix(LOG_DEBUG);
            printf("timer       | msgQSend: %d, %d, %d, %d\n", q_te_params.task_id,
                q_te_params.event_type, q_te_params.dl.tv_sec, q_te_params.dl.tv_nsec);
        }
    }
    /* activate scheduler */
    taskActivate(tidScheduler);

    /* get next queue time */
    intervaltimer.it_value.tv_sec = (*te_params).tp_params[0].qt.tv_sec;
    for (i=1; i<MAX_PENDING; i++) {
        if (((*te_params).tp_params[i].qt.tv_sec != 0) &&
            (intervaltimer.it_value.tv_sec > (*te_params).tp_params[i].qt.tv_sec)) {
            intervaltimer.it_value.tv_sec = (*te_params).tp_params[i].qt.tv_sec;
        }
    }
    
    print_log_prefix(LOG_DEBUG);
    printf("timer       | timer set to %ds\n", intervaltimer.it_value.tv_sec);

    /* mark tasks to be activated next*/
    for (i=0; i<MAX_PENDING; i++) {
        if (intervaltimer.it_value.tv_sec == (*te_params).tp_params[i].qt.tv_sec) {
            (*te_params).tp_params[i].is_set = true;
        }
    }

    /* set and arm timer */
    intervaltimer.it_value.tv_nsec = 0;
    intervaltimer.it_interval.tv_sec = 0;
    intervaltimer.it_interval.tv_nsec = 0;
    if (timer_settime(callingtimer, TIMER_ABSTIME, &intervaltimer, NULL) == ERROR ) {
        print_log_prefix(LOG_ERROR);
        printf("timer       | set_timer\n");
    }
}


/*************************************************************************/
/*  schduler task                                                        */
/*                                                                       */
/*************************************************************************/

void scheduler() {
    int i, j, id, d_priority, new_priority, old_priority, empty_idx;
    bool id_exists;
	q_te_param tr_param_temp;
    q_te_param q_te_params;
    q_te_param tr_params[MAX_READY];

    while (1) {
        /* get all pending time events */
        while (msgQNumMsgs(qidTimeEvents) > 0) {
            id_exists = false;
            if (msgQReceive(qidTimeEvents, (char*)&q_te_params, Q_TE_MSG_SIZE, NO_WAIT) == ERROR) {
                print_log_prefix(LOG_ERROR);
                printf("scheduler   | cannot receive queue msg (msgQReceive)");
            }
            id = q_te_params.task_id;
/*
            print_log_prefix(LOG_DEBUG);
            printf("scheduler   | task_id: %d\n", id);
            print_log_prefix(LOG_DEBUG);
            printf("scheduler   | event_type: %d\n", q_te_params.event_type);
            print_log_prefix(LOG_DEBUG);
            printf("scheduler   | dl[s]: %d\n", (int)q_te_params.dl.tv_sec);
            print_log_prefix(LOG_DEBUG);
            printf("scheduler   | dl[ns]: %d\n", (int)q_te_params.dl.tv_nsec);
*/
            if (q_te_params.event_type == ET_DEADLINE) {
                /* check if deadline has been violated */
                if(!taskIsSuspended(id)) {
                    print_log_prefix(LOG_WARNING);
                    printf("scheduler   | task (%s) missed deadline\n", taskName(id));
                }
            }
            else if (q_te_params.event_type == ET_ARRIVE) {
                /* set new dl in ready task array */
                empty_idx = -1;
                for (j=0; j<MAX_READY; j++) {
                    if (tr_params[j].task_id == id) {
                        tr_params[j].dl = q_te_params.dl;
						tr_params[j].event_type = ST_SUSPEND;
                        id_exists = true;
                        break;
                    }
                    else if ((tr_params[j].task_id == 0) || taskIdVerify(tr_params[j].task_id)) {
                        /* id does not exist or is zero */
                        tr_params[j].task_id = -1;
                        empty_idx = j;
                    }
                }
                if (!id_exists) {
                    /* add new ready entry */
                    if (empty_idx == -1) {
                        print_log_prefix(LOG_ERROR);
                        printf("scheduler   | task (%s) cannot be scheduled, to many active tasks\n", taskName(id));
                    }
                    else {
                        tr_params[empty_idx].task_id = id;
                        tr_params[empty_idx].dl = q_te_params.dl;
						tr_params[empty_idx].event_type = ST_SUSPEND;
                    }
                }
            }
            else {
                print_log_prefix(LOG_ERROR);
                printf("scheduler   | unknown event type: %d", q_te_params.event_type);
            }
        }

        /* order ready tasks by earliest dl */
        for (i=0; i<MAX_READY; i++) {
            for (j=i+1; j<MAX_READY; j++)
            {
                if (
                        (tr_params[i].dl.tv_sec > tr_params[j].dl.tv_sec) ||
                        (
                            (tr_params[i].dl.tv_sec == tr_params[j].dl.tv_sec) &&
                            (tr_params[i].dl.tv_nsec > tr_params[j].dl.tv_nsec)
                        )
                   )
                {
					tr_param_temp = tr_params[i];
                    tr_params[i] = tr_params[j];
                    tr_params[j] = tr_param_temp;
                }
            }
        }

        d_priority = 1;
        /* set priority accordingly and activate the task */
        for (i=0; i<MAX_READY; i++) {
            id = tr_params[i].task_id;
            if (id > 0) {
				taskPriorityGet(id, &old_priority);
				new_priority = MAX_PRIO + d_priority;
				if (new_priority >= MIN_PRIO) {
					new_priority = MIN_PRIO;
					print_log_prefix(LOG_WARNING);
					printf("scheduler   | min priority reached\n", taskName(id), id);
				}
				if (old_priority != new_priority) {
					taskPrioritySet(id, new_priority);
					print_log_prefix(LOG_DEBUG);
					printf("scheduler   | priority of task (%s|%d) set to %d\n", taskName(id), id, new_priority);
				}
				if (tr_params[i].event_type == ST_SUSPEND) {
					taskActivate(id);
					print_log_prefix(LOG_INFO);
					printf("scheduler   | task (%s|%d) activated\n", taskName(id), id);
					tr_params[i].event_type = ST_RUN;
				}
                d_priority++;
            }
        }
        taskSuspend(0);
    }
}


/*************************************************************************/
/*  periodic tasks                                                       */
/*                                                                       */
/*************************************************************************/

void periodic(int exec_time) {
    int tick_temp, cnt;
    while(1) {
        print_log_prefix(LOG_INFO);
        printf("%s | execution started\n", taskName(taskIdSelf()));
        cnt = 0;
		tick_temp = tickGet();
        while (cnt < exec_time*sysClkRateGet()) {
            if (tick_temp != tickGet()) {
				tick_temp = tickGet();
                cnt++;
            }
        }
        print_log_prefix(LOG_INFO);
        printf("%s | execution finished\n", taskName(taskIdSelf()));
        taskSuspend(0);
    }
}


/*************************************************************************/
/*  print log prefix                                                     */
/*                                                                       */
/*************************************************************************/

void print_log_prefix(int type) {
    const char* str_type;
    struct timespec mytime;
    if (type == LOG_ERROR) {
        str_type = "error  ";
    }
    else if (type == LOG_WARNING) {
        str_type = "warning";
    }
    else if (type == LOG_INFO) {
        str_type = "info   ";
    }
    else if (type == LOG_DEBUG) {
        str_type = "debug  ";
    }

    if ( clock_gettime (CLOCK_REALTIME, &mytime) == ERROR) {
        printf("----s ---ms | error   |              | clock_gettime\n", taskIdSelf());
        printf("----s ---ms | %s | ", str_type);
    }
    else {
        printf("%04ds %03dms | %s | ", (int)mytime.tv_sec, (int)(mytime.tv_nsec/1000000), str_type);
    }
}
