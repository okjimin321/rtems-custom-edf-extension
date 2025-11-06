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

rtems_task Periodic_Task_A( rtems_task_argument argument );
rtems_task Periodic_Task_B( rtems_task_argument argument );
rtems_task Init( rtems_task_argument argument );

volatile int task_completed_A = 0;
volatile int task_completed_B = 0;


rtems_task Periodic_Task_B( rtems_task_argument argument )
{
  
  rtems_status_code  status;
  rtems_id           period_id;
  rtems_interval     work_ticks = 500;   // 500틱 작업
  rtems_interval     period_ticks = 2200; // 0 tick에 등장하니 2000 tick을 deadline으로 설정하기 위함.

  // edf extension mode를 on.
  rtems_tcb* cur = _Thread_Get_executing();
  edf_set_execution_time(cur, work_ticks, period_ticks);

  puts( "Periodic B - Create Period" );
  status = rtems_rate_monotonic_create( rtems_build_name('H','I','G','H'), &period_id );
  directive_failed(status, "rate_monotonic_create");

  int first = 1;
  while (1) {

    status = rtems_rate_monotonic_period( period_id, period_ticks );
    //printf("Task B running... start Time: %u\n", rtems_clock_get_ticks_since_boot());
    rtems_interval start = rtems_clock_get_ticks_since_boot();
    rtems_interval end = start + work_ticks;
    edf_start_period(cur, start + period_ticks);

    if(first == 1){
      rtems_task_wake_after(1000);
      first = 0;
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
}

rtems_task Periodic_Task_A( rtems_task_argument argument )
{
  rtems_status_code  status;
  rtems_id           period_id;
  rtems_interval     work_ticks = 1500;  // 1500틱 작업
  rtems_interval     period_ticks = 2500; // 2500 tick이 deadline.

  // edf extension mode를 on 시킴.
  rtems_tcb* cur = _Thread_Get_executing();// 현재 thread 받아옴.
  edf_set_execution_time(cur, work_ticks, period_ticks);

  puts( "Periodic A - Create Period" );
  status = rtems_rate_monotonic_create( rtems_build_name('L','O','W',' '), &period_id );
  directive_failed(status, "rate_monotonic_create");

  while (1) {

    status = rtems_rate_monotonic_period( period_id, period_ticks );
    //printf("Task A running... start Time: %u\n", rtems_clock_get_ticks_since_boot());
    rtems_interval start = rtems_clock_get_ticks_since_boot();
    rtems_interval end = start + work_ticks;

    edf_start_period(cur, start + period_ticks);

    if (status != RTEMS_SUCCESSFUL) {
      if (status == RTEMS_TIMEOUT) {
        printf("Task A missed its deadline!\n");
      }
      directive_failed(status, "rate_monotonic_period");
    }

    while ( rtems_clock_get_ticks_since_boot() < end );
    //printf("Task A running... end Time: %u\n", rtems_clock_get_ticks_since_boot());
  }
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

  // 1. Task B 생성
  puts( "INIT - rtems_task_create - creating task 2" );
  status = rtems_task_create(
    rtems_build_name( 'T', 'A', '2', ' ' ),
    100, // 초기 우선순위는 EDF에서 중요하지 않음
    RTEMS_MINIMUM_STACK_SIZE,
    RTEMS_DEFAULT_MODES,
    RTEMS_DEFAULT_ATTRIBUTES,
    &task_id_B
  );
  directive_failed( status, "rtems_task_create of TA2" );

  // edf scheduler 할당
  puts( "INIT - rtems_task_set_scheduler - TA2 to EDF" );
  status = rtems_task_set_scheduler( task_id_B, edf_scheduler_id, 0); // EDF에서는 마지막 인자(우선순위) 무시
  directive_failed( status, "rtems_task_set_scheduler of TA2" );

  // 2. Task B 실행(실행하자 마자 1000 tick을 sleep함.)
  puts( "INIT - rtems_task_start - TA2 " );
  status = rtems_task_start( task_id_B, Periodic_Task_B, 0 ); // 인자 전달은 필요 없음
  directive_failed( status, "rtems_task_start of TA2(B)" );

  // 3. Task A 생성
  puts( "INIT - rtems_task_create - creating task 1" );
  status = rtems_task_create(
    rtems_build_name( 'T', 'A', '1', ' ' ),
    100, // 초기 우선순위는 EDF에서 중요하지 않음
    RTEMS_MINIMUM_STACK_SIZE,
    RTEMS_DEFAULT_MODES,
    RTEMS_DEFAULT_ATTRIBUTES,
    &task_id_A
  );
  directive_failed( status, "rtems_task_create of TA1" );

  // edf scheduler 할당
  puts( "INIT - rtems_task_set_scheduler - TA1 to EDF" );
  status = rtems_task_set_scheduler( task_id_A, edf_scheduler_id, 0); // EDF에서는 마지막 인자(우선순위) 무시
  directive_failed( status, "rtems_task_set_scheduler of TA1" );

  // 4. Task A 실행
  puts( "INIT - rtems_task_start - TA1 " );
  status = rtems_task_start( task_id_A, Periodic_Task_A, 0 ); // 인자 전달은 필요 없음
  directive_failed( status, "rtems_task_start of TA1(A)" );

  // 이 테스트는 무한 루프이므로 태스크 완료를 기다리지 않음.
  // 실제 실행 결과를 확인하려면 콘솔 출력을 분석해야 함.
  rtems_task_wake_after( rtems_clock_get_ticks_per_second() * 10 ); // 10초간 대기

  TEST_END();
  rtems_test_exit( 0 );
}

#define CONFIGURE_INIT
#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_SIMPLE_CONSOLE_DRIVER
#define CONFIGURE_MAXIMUM_TASKS           4
#define CONFIGURE_MAXIMUM_PERIODS         2
#define CONFIGURE_MAXIMUM_USER_EXTENSIONS 6 // edf extension 추가를 위해.
#define CONFIGURE_SCHEDULER_EDF
#define CONFIGURE_SCHEDULER_NAME_EDE_U = rtems_build_name('U', 'E', 'D', 'F')
#define CONFIGURE_INIT_TASK_ATTRIBUTES RTEMS_DEFAULT_MODES
#define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#define CONFIGURE_INIT
#include <rtems/confdefs.h>
