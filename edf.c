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
#define MAX_TASK      100
#define MAX_PERIOD    1000

typedef int bool;
#define true  1
#define false 0

/* task IDs */
int tidProdPeriodic;			
int tidProdAperiodic;			
int tidConsumer;

/* queue IDs */

/* function declarations */


/*************************************************************************/
/*  main task                                                            */
/*                                                                       */
/*************************************************************************/

int main(void) {
    struct timespec mytime;
    int    task_cnt = 0;
    int*   period;
    int*   exec_time;

    /* get the number of tasks */ 
    while ((task_cnt < 1) || (task_cnt > MAX_TASK)) {
        printf("Enter the number of tasks to be scheduled [1-%d]: ", MAX_TASK);
        scanf("%d", &task_cnt);
    };
    printf("Number of tasks set to %d.\n\n", task_cnt);

    exec_time = (int*)malloc(task_cnt*sizeof(int)+1);
    period = (int*)malloc(task_cnt*sizeof(int)+1);

    for (i = 0; i < task_cnt; i++){
        // get period of task i
        while ((period < 1) || (period > MAX_PERIOD)) {
            printf("Enter the period of task %d [1-%d]: ", i+1, MAX_PERIOD);
            scanf("%d", period);
        };
        printf("Period of task %d set to %d.\n\n", i+1, *period);

        // get execution time of task i
        while ((exec_time < 1) || (exec_time > *period)) {
            printf("Enter the execution time of task %d [1-%d]: ", i+1, *period);
            scanf("%d", exec_time);
        };
        printf("Execution time of task %d set to %d.\n\n", i+1, *exec_time);
        period++;
        exec_time++;
    }

    /* set clock to start at 0 */
    mytime.tv_sec  = 0;
    mytime.tv_nsec = 0;

    if (clock_settime(CLOCK_REALTIME, &mytime) < 0)
        printf("Error clock_settime\n");
    else
        printf("Current time set to %d sec %d ns \n\n",
                (int) mytime.tv_sec, (int)mytime.tv_nsec);

    printf("Exiting. \n\n");
    return(0);
}
