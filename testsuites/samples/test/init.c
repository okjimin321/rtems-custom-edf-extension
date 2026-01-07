/* SPDX-License-Identifier: BSD-2-Clause */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include<stdio.h>
#include <tmacros.h>
#include <rtems/cpuuse.h>
#include <rtems/score/thread.h>
#include <rtems.h>
#include <rtems/bspIo.h>

#include"/opt/rtems-6-sparc-gr712rc-smp-5/src/rtems/cpukit/include/rtems/edf_extension.h"
const char rtems_test_name[] = "SP 46";

bool test = 1;

/* Tasks:
 * Task B has an earlier first deadline than Task A and appears at tick 1000.
 * Its default behavior is to preempt Task A when it becomes ready.
 */
rtems_task Periodic_Task_A( rtems_task_argument argument );
rtems_task Periodic_Task_B( rtems_task_argument argument );
rtems_task Init( rtems_task_argument argument );

// This task appears at tick 1000
rtems_task Periodic_Task_B( rtems_task_argument argument )
{
  
  rtems_status_code  status;
  rtems_id           period_id;
  rtems_interval     work_ticks = 32;
  rtems_interval     period_ticks = 100;

  // Add edf_extension(for boosting)
  rtems_tcb* cur = _Thread_Get_executing();
  if(test)
    edf_set_execution_time(cur, work_ticks, period_ticks + 10);

  puts( "Periodic B - Create Period" );
  status = rtems_rate_monotonic_create( rtems_build_name('H','I','G','H'), &period_id );
  directive_failed(status, "rate_monotonic_create");
  
  int first = 1;
  for(int i = 0; i < 1600; i++) {
    rtems_interval start;
    rtems_interval end;
    if(first == 1){
      start = rtems_clock_get_ticks_since_boot();
      end = start + work_ticks;

      status = rtems_rate_monotonic_period( period_id, 110 );
      //edf_start_period(cur, 110);
      rtems_task_wake_after(10);
      first = 0;
    }
    else{
      status = rtems_rate_monotonic_period( period_id, period_ticks );
      //printf("Task B running... start Time: %u\n", rtems_clock_get_ticks_since_boot());
      start = rtems_clock_get_ticks_since_boot();
      end = start + work_ticks;

      //edf_start_period(cur, start + period_ticks);
      //printf("B start period: %d \n", start);
    }
    {
      //rtems_task_wake_after(3);
    }

    if (status != RTEMS_SUCCESSFUL) {
      if (status == RTEMS_TIMEOUT) {
        printf("Task B missed its deadline!\n");
      }
      directive_failed(status, "rate_monotonic_period");
    }

    while ( rtems_clock_get_ticks_since_boot() < end );
    //printf("Task B running... end Time: %u\n", rtems_clock_get_ticks_since_boot());
  }
  printf("B ends \n");
}

rtems_task Periodic_Task_A( rtems_task_argument argument )
{
  rtems_status_code  status;
  rtems_id           period_id;
  rtems_interval     work_ticks = 30;
  rtems_interval     period_ticks = 200;

  // Add edf_extension(for boosting)
  rtems_tcb* cur = _Thread_Get_executing();
  if(test)
    edf_set_execution_time(cur, work_ticks, period_ticks);

  puts( "Periodic A - Create Period" );
  status = rtems_rate_monotonic_create( rtems_build_name('L','O','W',' '), &period_id );
  directive_failed(status, "rate_monotonic_create");

  for(int i = 0; i < 800; i++) {

    status = rtems_rate_monotonic_period( period_id, period_ticks );
    rtems_interval start = rtems_clock_get_ticks_since_boot();
    rtems_interval end = start + work_ticks;

    if (status != RTEMS_SUCCESSFUL) {
      if (status == RTEMS_TIMEOUT) {
        printf("Task A missed its deadline!\n");
      }
      directive_failed(status, "rate_monotonic_period");
    }

    while ( rtems_clock_get_ticks_since_boot() < end );
  }
  printf("A ends \n");
}


rtems_task Init( rtems_task_argument argument )
{

  initialize_edf_extension();

  rtems_status_code  status;
  rtems_id           task_id_A;
  rtems_id           task_id_B;
  rtems_id           edf_scheduler_id;

  TEST_BEGIN();

  puts( "INIT - rtems_scheduler_ident - getting EDF scheduler" );
  status = rtems_scheduler_ident(
      rtems_build_name( 'U', 'E', 'D', 'F' ),
      &edf_scheduler_id
  );
  directive_failed( status, "rtems_scheduler_ident(EDF)" );

  // 1. Create Task B
  puts( "INIT - rtems_task_create - creating task 2" );
  status = rtems_task_create(
    rtems_build_name( 'T', 'A', '2', ' ' ),
    100, // Priority is irrelevant under EDF schdeuling
    RTEMS_MINIMUM_STACK_SIZE,
    RTEMS_DEFAULT_MODES,
    RTEMS_DEFAULT_ATTRIBUTES,
    &task_id_B
  );
  directive_failed( status, "rtems_task_create of TA2" );

  // Assign EDF scheduler for Task B
  puts( "INIT - rtems_task_set_scheduler - TA2 to EDF" );
  status = rtems_task_set_scheduler( task_id_B, edf_scheduler_id, 0);
  directive_failed( status, "rtems_task_set_scheduler of TA2" );

  // 2. Execute Task B (sleeps for 1000 ticks immediately after starting)
  puts( "INIT - rtems_task_start - TA2 " );
  status = rtems_task_start( task_id_B, Periodic_Task_B, 0 );
  directive_failed( status, "rtems_task_start of TA2(B)" );

  // 3. Create Task A
  puts( "INIT - rtems_task_create - creating task 1" );
  status = rtems_task_create(
    rtems_build_name( 'T', 'A', '1', ' ' ),
    100, // Priority is irrelevant under EDF schdeuling
    RTEMS_MINIMUM_STACK_SIZE,
    RTEMS_DEFAULT_MODES,
    RTEMS_DEFAULT_ATTRIBUTES,
    &task_id_A
  );
  directive_failed( status, "rtems_task_create of TA1" );

  // Assign EDF scheduler for Task A
  puts( "INIT - rtems_task_set_scheduler - TA1 to EDF" );
  status = rtems_task_set_scheduler( task_id_A, edf_scheduler_id, 0);
  directive_failed( status, "rtems_task_set_scheduler of TA1" );

  // 4. Execute Task A
  puts( "INIT - rtems_task_start - TA1 " );
  status = rtems_task_start( task_id_A, Periodic_Task_A, 0 );
  directive_failed( status, "rtems_task_start of TA1(A)" );

  // After 10 seconds, Test terminates
  rtems_task_wake_after( rtems_clock_get_ticks_per_second() * 5000 );

  TEST_END();
  rtems_test_exit( 0 );
}

#define CONFIGURE_INIT
#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_SIMPLE_CONSOLE_DRIVER
#define CONFIGURE_MAXIMUM_TASKS           4
#define CONFIGURE_MAXIMUM_PERIODS         2
#define CONFIGURE_MAXIMUM_USER_EXTENSIONS 6 // For adding edf_extension
#define CONFIGURE_SCHEDULER_EDF

#define CONFIGURE_INIT_TASK_ATTRIBUTES RTEMS_DEFAULT_MODES
#define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#define CONFIGURE_INIT
#include <rtems/confdefs.h>
