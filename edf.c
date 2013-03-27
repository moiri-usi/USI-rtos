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
#define MAX_PERIOD    100
#define MAX_DEADLINE  100
#define MAX_PRIO      102

#define WAITING 0
#define READY   1
#define RUNNING 2
#define DONE    3

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

typedef struct queue_param {
    struct timespec qt;
    int period;
    int id;
    int status;
} q_param;

/* task IDs */
int tidTimerMux;

/* function declarations */
void timerMux(t_param*, int);
void scheduler(timer_t, q_param*);
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

    /* create periodic tasks */
    for (i=0; i<task_cnt; i++) {
		sprintf(t_name, "tPeriodic_%d", i);
        t_params[i].id = taskCreate(t_name, 255, 0, STACK_SIZE,
            (FUNCPTR)periodic, t_params[i].exec_time, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    }

    /* run for given simulation time */
    taskDelay(nseconds*60);

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
    q_param pending_tasks[MAX_PERIODIC];

	/* create timer */
	if ( timer_create(CLOCK_REALTIME, NULL, &ptimer) == ERROR)
		printf("Error create_timer\n");

    /* initialize pending_tasks array */
    for (i=0; i<MAX_PERIODIC; i++) {
        pending_tasks[i].status = (i < task_cnt) ? READY : WAITING;
        pending_tasks[i].id = t_params[i].id;
        pending_tasks[i].period = t_params[i].period;
        pending_tasks[i].qt.tv_sec = 0;
        pending_tasks[i].qt.tv_nsec = 1;
    }

	/* connect timer to timer handler routine */
	if ( timer_connect(ptimer, (VOIDFUNCPTR)scheduler, (int)pending_tasks) == ERROR )
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
/*  schduler task                                                        */
/*                                                                       */
/*************************************************************************/

void scheduler(timer_t callingtimer, q_param* pending_tasks) {
    int i, j, id, period;
	struct itimerspec intervaltimer;

    /* set priority of arrived task according to its deadline */
    for (i=0; i<MAX_PERIODIC; i++) {
        id = pending_tasks[i].id;
        period = pending_tasks[i].period;
        if (pending_tasks[i].status == READY) {
            taskPrioritySet(id, MAX_PRIO + period);
            /* activate the task */
            taskActivate(id);
            print_log_prefix(LOG_INFO);
            printf("scheduler   | task (%s) activated\n", taskName(id));
            pending_tasks[i].status = RUNNING;
			/* set the new queue time */
			pending_tasks[i].qt.tv_sec = pending_tasks[i].qt.tv_sec + period;
        }
        else if (pending_tasks[i].status == RUNNING && !taskIsSuspended(id)) {
            /* restart the task (missed deadline) */
            print_log_prefix(LOG_WARNING);
            printf("scheduler   | task (%s) missed deadline\n", taskName(id));
            if (taskRestart(id) == ERROR) {
                print_log_prefix(LOG_ERROR);
                printf("scheduler   | task (%s) cannot restart\n", taskName(id));
            }
			/* set the new queue time */
			pending_tasks[i].qt.tv_sec = pending_tasks[i].qt.tv_sec + period;
        }
        else if (pending_tasks[i].status == RUNNING && taskIsSuspended(id)) {
            /* task was executed in time */
            pending_tasks[i].status = WAITING;
			print_log_prefix(LOG_DEBUG);
			printf("scheduler   | task (%s) executed in time\n", taskName(id));
        }
    }

    /* get next queue time */
	intervaltimer.it_value.tv_sec = pending_tasks[0].qt.tv_sec;
    for (i=1; i<MAX_PERIODIC; i++) {
        if ((intervaltimer.it_value.tv_sec > pending_tasks[i].qt.tv_sec)
				&& (pending_tasks[i].qt.tv_sec != 0)) {
            intervaltimer.it_value.tv_sec = pending_tasks[i].qt.tv_sec;
        }
    }
	
	print_log_prefix(LOG_DEBUG);
	printf("scheduler   | timer set to %ds\n", intervaltimer.it_value.tv_sec);

    /* mark tasks to be activated next*/
    for (i=0; i<MAX_PERIODIC; i++) {
        if (intervaltimer.it_value.tv_sec == pending_tasks[i].qt.tv_sec) {
            pending_tasks[i].status = READY;
        }
    }

	/* set and arm timer */
	intervaltimer.it_value.tv_nsec = 0;
	intervaltimer.it_interval.tv_sec = 0;
	intervaltimer.it_interval.tv_nsec = 0;
	if (timer_settime(callingtimer, TIMER_ABSTIME, &intervaltimer, NULL) == ERROR ) {
        print_log_prefix(LOG_ERROR);
        printf("scheduler   | set_timer\n");
    }
}


/*************************************************************************/
/*  periodic tasks                                                       */
/*                                                                       */
/*************************************************************************/

void periodic(int exec_time) {
    while(1) {
        print_log_prefix(LOG_INFO);
        printf("%s | execution started\n", taskName(taskIdSelf()));
        taskDelay(exec_time*60);
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
        printf("----s | error   |              | clock_gettime\n", taskIdSelf());
        printf("----s | %s | ", str_type);
    }
    else {
        printf("%04ds | %s | ", (int)mytime.tv_sec, str_type);
    }
}
