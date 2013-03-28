/*****************************************************************************/
/*  edf.c                                                                    */
/*                                                                           */
/*  Simon Maurer                                                             */
/*  2x.03.2013                                                               */
/*                                                                           */
/*****************************************************************************/

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
#define MAX_READY     (MAX_PERIODIC + MAX_APERIODIC)
#define MAX_PERIOD    100
#define MAX_DEADLINE  100
#define MAX_PRIO      104
#define MIN_PRIO      255

#define ET_ARRIVE     0
#define ET_DEADLINE   1

#define TT_PERIODIC   0
#define TT_APERIODIC  1

#define QS_WAITING4Q  0
#define QS_READY2Q    1
#define QS_QUEUED     2

#define Q_TE_MSG_SIZE sizeof(q_te_param)

#define LOG_INFO      0
#define LOG_WARNING   1
#define LOG_ERROR     2
#define LOG_DEBUG     3

typedef int bool;
#define true  1
#define false 0

/* initial parameters of periodic tasks */
typedef struct task_param {
    int period;
    int exec_time;
    int id;
} t_param;

/* parameters of pending tasks (to be 
 * converted in timie events)*/
typedef struct task_pending_param {
	int q_state;
    int id;
    struct timespec qt;
} tp_param;

/* parameters of ready tasks, being executed 
 * as soon as they get cpu time */
typedef struct task_ready_param {
    bool is_scheduled;
    int id;
    struct timespec dl;
} tr_param;

/* helper structure to be able to pass multiple 
 * structures to the timer handler */
typedef struct time_event_param {
    t_param* t_params;
    tp_param tpp_params[MAX_PERIODIC];
    tp_param tpa_params[MAX_APERIODIC];
    tp_param last_aperiodic_task;
	int cnt_aperiodic;
} te_param;

/* message structure used in the time event queue */
typedef struct queue_time_event_param {
    int task_id;
    int task_type;
    int event_type;
    struct timespec dl;
} q_te_param;

/* task IDs */
int tidTimerMux;
int tidScheduler;
int tidServer;

/* queue IDs */
MSG_Q_ID qidTimeEvents;

/* function declarations */
void timerMux(te_param*, timer_t*);
void timerHandler(timer_t, te_param*);
void server(te_param*, float*, timer_t*);
void scheduler();
void user_task(int);
void print_log_prefix(int);
void send2q(int, int, int, struct timespec);
void set_new_timer(te_param*, timer_t*);


/*************************************************************************/
/*  main task                                                            */
/*                                                                       */
/*************************************************************************/

int main(void) {
    struct  timespec mytime;
	float   utilisation = 0.0;
	float   temp_util = 0.0;
    int     task_cnt = 0;
    int     nseconds = 0;
    int     i;
    t_param t_params[MAX_PERIODIC];
	te_param te_params;
    timer_t ptimer;
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
		temp_util = 0.0;
        while ((t_params[i].period < 1) || (t_params[i].period > MAX_PERIOD)) {
            printf("Enter the period of task %d [1-%d]s: ", i+1, MAX_PERIOD);
            scanf("%d", &t_params[i].period);
        };
        printf("Period of task %d set to %d.\n\n", i+1, t_params[i].period);

        // get execution time of task i
        while ((t_params[i].exec_time < 1) ||
                (t_params[i].exec_time > t_params[i].period) || (temp_util >= 1.0)) {
			if (temp_util >= 1.0) {
				printf("Utilisation to high, enter lower execution time!\n");
			}
            printf("Enter the execution time of task %d [1-%d]s: ", i+1, t_params[i].period);
            scanf("%d", &t_params[i].exec_time);
			temp_util = utilisation + (float)t_params[i].exec_time/(float)t_params[i].period;
        };
		utilisation = temp_util;
        printf("Execution time of task %d set to %d.\n", i+1, t_params[i].exec_time);
		printf("Utilisation: %f\n\n", utilisation);
    }

    /* set clock to start at 0 */
    mytime.tv_sec  = 0;
    mytime.tv_nsec = 0;
	
	/* initialize timing event parameters */
	te_params.t_params = t_params;
	te_params.cnt_aperiodic = 0;
	te_params.last_aperiodic_task.id = 0;
	te_params.last_aperiodic_task.q_state = QS_WAITING4Q;
	te_params.last_aperiodic_task.qt.tv_sec = 0;
	te_params.last_aperiodic_task.qt.tv_nsec = 0;
	for (i=0; i<MAX_PERIODIC; i++) {
		te_params.tpp_params[i].q_state = (i < task_cnt) ? QS_READY2Q : QS_WAITING4Q;
		te_params.tpp_params[i].qt.tv_sec = 0;
		te_params.tpp_params[i].qt.tv_nsec = 0;
	}
	for (i=0; i<MAX_APERIODIC; i++) {
		te_params.tpa_params[i].q_state = QS_WAITING4Q;
		te_params.tpa_params[i].qt.tv_sec = 0;
		te_params.tpa_params[i].qt.tv_nsec = 0;
	}

    if (clock_settime(CLOCK_REALTIME, &mytime) < 0)
        printf("Error clock_settime\n");
    else
        printf("Current time set to %d sec %d ns \n\n",
                (int) mytime.tv_sec, (int)mytime.tv_nsec);

    /* spawn (create and start) timer task */
    tidTimerMux = taskSpawn("tTimerMux", 101, 0, STACK_SIZE,
        (FUNCPTR)timerMux, (int)&te_params, (int)&ptimer, 0, 0, 0, 0, 0, 0, 0, 0);
		
	/* spawn (create and start) server task */
	tidServer = taskSpawn("tServer", 102, 0, STACK_SIZE,
		(FUNCPTR)server, (int)&te_params, (int)&utilisation, (int)&ptimer, 0, 0, 0, 0, 0, 0, 0);

    /* create scheduler task */
    tidScheduler = taskCreate("tScheduler", 103, 0, STACK_SIZE,
        (FUNCPTR)scheduler, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

    /* create periodic tasks */
    for (i=0; i<task_cnt; i++) {
        sprintf(t_name, "tPeriodic_%d", i);
        t_params[i].id = taskCreate(t_name, 255, 0, STACK_SIZE,
            (FUNCPTR)user_task, t_params[i].exec_time, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        print_log_prefix(LOG_DEBUG);
        printf("main        | task created (%s|%d)\n", t_name, t_params[i].id);
    }

    /* run for given simulation time */
    taskDelay(nseconds*sysClkRateGet());

    /* create periodic tasks */
    for (i=0; i<task_cnt; i++) {
        taskDelete(t_params[i].id);
    }
	taskDelete(tidScheduler);
	taskDelete(tidServer);
    taskDelete(tidTimerMux);

    printf("Exiting. \n\n");
    return(0);
}


/******************************************************************************/
/*  multiplexed timer task                                                    */
/*                                                                            */
/**********************************************************************i*******/

void timerMux(te_param* te_params, timer_t* ptimer) {
    int i;
    struct itimerspec intervaltimer;

    /* create message queue */
    if ((qidTimeEvents = msgQCreate (MAX_READY*2, Q_TE_MSG_SIZE, MSG_Q_PRIORITY)) == NULL)
        printf("Error msgQCreate\n");

    /* create timer */
    if ( timer_create(CLOCK_REALTIME, NULL, ptimer) == ERROR)
        printf("Error create_timer\n");

    /* connect timer to timer handler routine */
    if ( timer_connect(*ptimer, (VOIDFUNCPTR)timerHandler, (int)te_params) == ERROR )
        printf("Error connect_timer\n");

    /* set and arm timer */
    intervaltimer.it_value.tv_sec = 0;
    intervaltimer.it_value.tv_nsec = 1;
    intervaltimer.it_interval.tv_sec = 0;
    intervaltimer.it_interval.tv_nsec = 0;

    if ( timer_settime(*ptimer, TIMER_ABSTIME, &intervaltimer, NULL) == ERROR )
        printf("Error set_timer\n");

    /* idle loop */
    while(1) pause();
}


/*************************************************************************/
/*  timer handler                                                        */
/*                                                                       */
/*************************************************************************/

void timerHandler(timer_t callingtimer, te_param* te_params) {
    int i;
    q_te_param q_te_params;

    /* convert periodic tasks to tome events */
    for (i=0; i<MAX_PERIODIC; i++) {
        if ((*te_params).tpp_params[i].q_state == QS_READY2Q) {
            /* send time events */
            if ((*te_params).tpp_params[i].qt.tv_sec != 0) {
                /* add deadline time event to the queue */
                send2q((*te_params).t_params[i].id, TT_PERIODIC, ET_DEADLINE,
                        (*te_params).tpp_params[i].qt);
            }
            /* set new queue times */
            (*te_params).tpp_params[i].qt.tv_sec += (*te_params).t_params[i].period;
            (*te_params).tpp_params[i].q_state = QS_WAITING4Q;
            /* add arrive time event to the queue */ 
            send2q((*te_params).t_params[i].id, TT_PERIODIC, ET_ARRIVE,
                    (*te_params).tpp_params[i].qt);
        }
    }

    /* convert aperiodic tasks to tome events */
    for (i=0; i<MAX_APERIODIC; i++) {
        if ((*te_params).tpa_params[i].q_state == QS_READY2Q) {
            /* send time events */
            /* add deadline time event to the queue */ 
            send2q((*te_params).tpa_params[i].id, TT_APERIODIC, ET_DEADLINE,
                    (*te_params).tpa_params[i].qt);
            (*te_params).tpa_params[i].q_state = QS_QUEUED;
        }
    }

    /* activate scheduler */
    taskActivate(tidScheduler);

    /* set next timer event */
    set_new_timer(te_params, &callingtimer);
}


/*************************************************************************/
/*  server task                                                          */
/*                                                                       */
/*************************************************************************/

void server(te_param* te_params, float* utilisation, timer_t* ptimer) {
	char cmd;
    char t_name[20];
	int exec_time, id, empty_idx, i, util_sec;
    struct timespec now, last_dl, dl;
	while (1) {
		cmd = '0';
		exec_time = 0;
		while (cmd != 'n') {
			scanf("%c", &cmd);
		};
		/* create task if possibe and update te_params */
        empty_idx = -1;
        for (i=0; i<MAX_APERIODIC; i++) {
            if (((*te_params).tpa_params[i].id == 0) || 
                    taskIdVerify((*te_params).tpa_params[i].id)) {
                /* id does not exist or is zero */
                empty_idx = i;
                break;
            }
        }
        if (empty_idx < 0) {
            /* no empty spot in pendin array */
            print_log_prefix(LOG_WARNING);
            printf("server      | too many pending aperiodic tasks, aborting...\n", t_name, id);
        }
        else {
            while ((exec_time < 1) || (exec_time > MAX_PERIOD)) {
                printf("Enter the execution time of aperiodic task [1-%d]s: ", MAX_PERIOD);
                scanf("%d", &exec_time);
            };

            if ( clock_gettime (CLOCK_REALTIME, &now) == ERROR) {
                printf("----s ---ms | error   |              | clock_gettime\n", taskIdSelf());
            }

            /* create task */
            (*te_params).cnt_aperiodic++;
            sprintf(t_name, "tAperiodic_%d", (*te_params).cnt_aperiodic);
            id = taskCreate(t_name, 255, 0, STACK_SIZE, (FUNCPTR)user_task, exec_time, 
                    0, 0, 0, 0, 0, 0, 0, 0, 0);
            print_log_prefix(LOG_DEBUG);
            printf("server      | task created (%s|%d)\n", t_name, id);

            last_dl = now;
            /* get the deadline from the last aperiodic task */
            if (((*te_params).last_aperiodic_task.qt.tv_sec > now.tv_sec) ||
                    (
                     ((*te_params).last_aperiodic_task.qt.tv_sec == now.tv_sec) &&
                     ((*te_params).last_aperiodic_task.qt.tv_nsec > now.tv_nsec)
                    )
               )
            {
                last_dl = (*te_params).last_aperiodic_task.qt;
            }

            /* calculate the deadline */
			util_sec = (int)(exec_time/(*utilisation));
            dl.tv_sec = last_dl.tv_sec + util_sec;
            dl.tv_nsec = last_dl.tv_nsec + (int)((exec_time/(*utilisation) - (float)util_sec)*1000000000);
			print_log_prefix(LOG_DEBUG);
			printf("server      | dl of task (%s|%d) set to %ds %dns\n", t_name, id, dl.tv_sec, dl.tv_nsec);

			/* aperiodic task arrived, queue time event */
			send2q(id, TT_APERIODIC, ET_ARRIVE, dl);

            /* update te_params */
            (*te_params).tpa_params[i].id = id;
            (*te_params).tpa_params[i].qt = dl;
            (*te_params).tpa_params[i].q_state = QS_WAITING4Q;

            /* set next timer event */
            set_new_timer(te_params, ptimer);
			
			/* activate scheduler */
			taskActivate(tidScheduler);
        }
	}
}


/*************************************************************************/
/*  scheduler task                                                       */
/*                                                                       */
/*************************************************************************/

void scheduler() {
    int i, j, id, d_priority, new_priority, old_priority, empty_idx, idx;
    bool id_exists;
    q_te_param q_te_params;
	tr_param tr_param_temp;
    tr_param tr_params[MAX_READY];
	
	/* initialise tr_params */
	for (i=0; i<MAX_READY; i++) {
		tr_params[i].id = 0;
		tr_params[i].is_scheduled = false;
		tr_params[i].dl.tv_sec = 0;
		tr_params[i].dl.tv_nsec = 0;
	}

    while (1) {
        /* get all pending time events */
        while (msgQNumMsgs(qidTimeEvents) > 0) {
            id_exists = false;

            /* read queue */
            if (msgQReceive(qidTimeEvents, (char*)&q_te_params, Q_TE_MSG_SIZE,
                        NO_WAIT) == ERROR) {
                print_log_prefix(LOG_ERROR);
                printf("scheduler   | cannot receive queue msg (msgQReceive)");
            }
            id = q_te_params.task_id;
			
			print_log_prefix(LOG_DEBUG);
			printf("scheduler   | msgQReceive: %d, %d, %d, %d\n", q_te_params.task_id,
			q_te_params.event_type, q_te_params.dl.tv_sec, q_te_params.dl.tv_nsec);

            if (q_te_params.event_type == ET_DEADLINE) {
                /* check if deadline has been violated */
                if(!taskIsSuspended(id)) {
                    print_log_prefix(LOG_WARNING);
                    printf("scheduler   | task (%s) missed deadline\n", taskName(id));
                    if (q_te_params.task_type == TT_PERIODIC) {
                        taskRestart(id);
                        taskSuspend(id);
                    }
                }
                if (q_te_params.task_type == TT_APERIODIC) {
                    /* delete the aperiodic task (it does not matter if
                     * it missed the deadline or not)*/
                    taskDelete(id);
                }
            }
            else if (q_te_params.event_type == ET_ARRIVE) {
                /* set new dl in ready task array */
                empty_idx = -1;
                for (j=0; j<MAX_READY; j++) {
                    if (tr_params[j].id == id) {
                        tr_params[j].dl = q_te_params.dl;
						tr_params[j].is_scheduled = false;
						idx = j;
                        id_exists = true;
                        break;
                    }
                    else if ((tr_params[j].id == 0) || taskIdVerify(tr_params[j].id)) {
                        /* id does not exist or is zero */
                        tr_params[j].id = 0;
                        idx = empty_idx = j;
                    }
                }
                if (!id_exists) {
                    /* add new ready entry */
                    if (empty_idx == -1) {
                        print_log_prefix(LOG_ERROR);
                        printf("scheduler   | task (%s) cannot be scheduled, to many active tasks\n", taskName(id));
                    }
                    else {
                        tr_params[empty_idx].id = id;
                        tr_params[empty_idx].dl = q_te_params.dl;
						tr_params[empty_idx].is_scheduled = false;
                    }
                }
				/*
				print_log_prefix(LOG_DEBUG);
				printf("scheduler   | ready task set: %d, %d, %d, %d, %d\n", tr_params[idx].id,
					tr_params[idx].is_scheduled, tr_params[idx].dl.tv_sec, tr_params[idx].dl.tv_nsec, idx);
					*/
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
            id = tr_params[i].id;
			/*
			print_log_prefix(LOG_DEBUG);
			printf("scheduler   | ready task set (ordered): %d, %d, %d, %d\n", tr_params[i].id,
				tr_params[i].is_scheduled, tr_params[i].dl.tv_sec, tr_params[i].dl.tv_nsec);
				*/
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
				if (tr_params[i].is_scheduled == false) {
					taskActivate(id);
					print_log_prefix(LOG_INFO);
					printf("scheduler   | task (%s|%d) activated\n", taskName(id), id);
					tr_params[i].is_scheduled = true;
				}
                d_priority++;
            }
        }
        taskSuspend(0);
    }
}


/*************************************************************************/
/*  user tasks                                                           */
/*                                                                       */
/*************************************************************************/

void user_task(int exec_time) {
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


/*************************************************************************/
/*  send messages to queue                                               */
/*                                                                       */
/*************************************************************************/

void send2q(int id, int task_type, int event_type, struct timespec qt) {
    q_te_param q_te_params;
    q_te_params.task_id = id;
    q_te_params.task_type = task_type;
    q_te_params.event_type = event_type;
    q_te_params.dl = qt;
    if (msgQSend (qidTimeEvents, (char*)&q_te_params, Q_TE_MSG_SIZE, NO_WAIT, MSG_PRI_NORMAL) == ERROR) {
        print_log_prefix(LOG_ERROR);
        printf("timer       | cannot send queue dl msg (msgQSend)\n");
    }
    print_log_prefix(LOG_DEBUG);
    printf("timer       | msgQSend: %d, %d, %d, %d\n", q_te_params.task_id,
            q_te_params.event_type, q_te_params.dl.tv_sec, q_te_params.dl.tv_nsec);
}


/*************************************************************************/
/*  set new timer for timerHandler                                       */
/*                                                                       */
/*************************************************************************/

void set_new_timer(te_param* te_params, timer_t* ptimer) {
    int i;
    struct itimerspec intervaltimer;
    /* get next queue time */
    intervaltimer.it_value.tv_sec = (*te_params).tpp_params[0].qt.tv_sec;
    for (i=1; i<MAX_PERIODIC; i++) {
        if (((*te_params).tpp_params[i].qt.tv_sec != 0) &&
            (intervaltimer.it_value.tv_sec > (*te_params).tpp_params[i].qt.tv_sec)) {
            intervaltimer.it_value.tv_sec = (*te_params).tpp_params[i].qt.tv_sec;
        }
    }
    for (i=0; i<MAX_APERIODIC; i++) {
        if (((*te_params).tpa_params[i].q_state != QS_QUEUED) && 
			((*te_params).tpa_params[i].qt.tv_sec != 0) &&
            (intervaltimer.it_value.tv_sec > (*te_params).tpa_params[i].qt.tv_sec)) {
            intervaltimer.it_value.tv_sec = (*te_params).tpa_params[i].qt.tv_sec;
        }
    }
    
    print_log_prefix(LOG_DEBUG);
    printf("timer       | timer set to %ds\n", intervaltimer.it_value.tv_sec);

    /* mark tasks to be activated next */
    for (i=0; i<MAX_PERIODIC; i++) {
		(*te_params).tpp_params[i].q_state = QS_WAITING4Q;
        if (intervaltimer.it_value.tv_sec == (*te_params).tpp_params[i].qt.tv_sec) {
            (*te_params).tpp_params[i].q_state = QS_READY2Q;
        }
    }
    for (i=0; i<MAX_APERIODIC; i++) {
		if ((*te_params).tpa_params[i].q_state != QS_QUEUED) {
			(*te_params).tpa_params[i].q_state = QS_WAITING4Q;
		}
        if (intervaltimer.it_value.tv_sec == (*te_params).tpa_params[i].qt.tv_sec) {
            (*te_params).tpa_params[i].q_state = QS_READY2Q;
        }
    }

    /* set and arm timer */
    intervaltimer.it_value.tv_nsec = 0;
    intervaltimer.it_interval.tv_sec = 0;
    intervaltimer.it_interval.tv_nsec = 0;
    if (timer_settime(*ptimer, TIMER_ABSTIME, &intervaltimer, NULL) == ERROR ) {
        print_log_prefix(LOG_ERROR);
        printf("timer       | set_timer\n");
    }

}
