/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file
 *
 * @ingroup RTEMSScoreThread
 *
 * @brief This source file contains the definition of ::_Thread_Allocated_fp
 *   and ::_User_extensions_Switches_list and the implementation of
 *   _Thread_Dispatch_direct(), _Thread_Dispatch_enable(),
 *   and _Thread_Do_dispatch().
 */

/*
 *  COPYRIGHT (c) 1989-2009.
 *  On-Line Applications Research Corporation (OAR).
 *
 *  Copyright (c) 2014, 2018 embedded brains GmbH.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <rtems/score/threaddispatch.h>
#include <rtems/score/assert.h>
#include <rtems/score/isr.h>
#include <rtems/score/schedulerimpl.h>
#include <rtems/score/threadimpl.h>
#include <rtems/score/todimpl.h>
#include <rtems/score/userextimpl.h>
#include <rtems/config.h>

// edf extension 추가.
#include <stdio.h>
#include <rtems.h>
#include <rtems/score/thread.h>
#include <stdbool.h>
#include <rtems/edf_extension.h>
#include <rtems/rtems/clock.h> // 현재 시간을 가져오기 위해 추가.

#if ( CPU_HARDWARE_FP == TRUE ) || ( CPU_SOFTWARE_FP == TRUE )
Thread_Control *_Thread_Allocated_fp;
#endif

CHAIN_DEFINE_EMPTY( _User_extensions_Switches_list );

#if defined(RTEMS_SMP)
static ISR_Level _Thread_Check_pinning(
  Thread_Control  *executing,
  Per_CPU_Control *cpu_self,
  ISR_Level        level
)
{
  unsigned int pin_level;

  pin_level = executing->Scheduler.pin_level;

  if (
    RTEMS_PREDICT_FALSE( pin_level != 0 )
      && ( pin_level & THREAD_PIN_PREEMPTION ) == 0
  ) {
    ISR_lock_Context         state_lock_context;
    ISR_lock_Context         scheduler_lock_context;
    const Scheduler_Control *pinned_scheduler;
    Scheduler_Node          *pinned_node;
    const Scheduler_Control *home_scheduler;

    _ISR_Local_enable( level );

    executing->Scheduler.pin_level = pin_level | THREAD_PIN_PREEMPTION;

    _Thread_State_acquire( executing, &state_lock_context );

    pinned_scheduler = _Scheduler_Get_by_CPU( cpu_self );
    pinned_node = _Thread_Scheduler_get_node_by_index(
      executing,
      _Scheduler_Get_index( pinned_scheduler )
    );

    if ( _Thread_Is_ready( executing ) ) {
      _Scheduler_Block( executing);
    }

    home_scheduler = _Thread_Scheduler_get_home( executing );
    executing->Scheduler.pinned_scheduler = pinned_scheduler;

    if ( home_scheduler != pinned_scheduler ) {
      _Chain_Extract_unprotected( &pinned_node->Thread.Scheduler_node.Chain );
      _Chain_Prepend_unprotected(
        &executing->Scheduler.Scheduler_nodes,
        &pinned_node->Thread.Scheduler_node.Chain
      );
    }

    _Scheduler_Acquire_critical( pinned_scheduler, &scheduler_lock_context );

    ( *pinned_scheduler->Operations.pin )(
      pinned_scheduler,
      executing,
      pinned_node,
      cpu_self
    );

    if ( _Thread_Is_ready( executing ) ) {
      ( *pinned_scheduler->Operations.unblock )(
        pinned_scheduler,
        executing,
        pinned_node
      );
    }

    _Scheduler_Release_critical( pinned_scheduler, &scheduler_lock_context );

    _Thread_State_release( executing, &state_lock_context );

    _ISR_Local_disable( level );
  }

  return level;
}

static void _Thread_Ask_for_help( Thread_Control *the_thread )
{
  Chain_Node       *node;
  const Chain_Node *tail;

  node = _Chain_First( &the_thread->Scheduler.Scheduler_nodes );
  tail = _Chain_Immutable_tail( &the_thread->Scheduler.Scheduler_nodes );

  do {
    Scheduler_Node          *scheduler_node;
    const Scheduler_Control *scheduler;
    ISR_lock_Context         lock_context;
    bool                     success;

    scheduler_node = SCHEDULER_NODE_OF_THREAD_SCHEDULER_NODE( node );
    scheduler = _Scheduler_Node_get_scheduler( scheduler_node );

    _Scheduler_Acquire_critical( scheduler, &lock_context );
    success = ( *scheduler->Operations.ask_for_help )(
      scheduler,
      the_thread,
      scheduler_node
    );
    _Scheduler_Release_critical( scheduler, &lock_context );

    if ( success ) {
      break;
    }

    node = _Chain_Next( node );
  } while ( node != tail );
}

static bool _Thread_Can_ask_for_help( const Thread_Control *executing )
{
  return executing->Scheduler.helping_nodes > 0
    && _Thread_Is_ready( executing );
}
#endif

static ISR_Level _Thread_Preemption_intervention(
  Thread_Control  *executing,
  Per_CPU_Control *cpu_self,
  ISR_Level        level
)
{
#if defined(RTEMS_SMP)
  ISR_lock_Context lock_context;

  level = _Thread_Check_pinning( executing, cpu_self, level );

  _Per_CPU_Acquire( cpu_self, &lock_context );

  while ( !_Chain_Is_empty( &cpu_self->Threads_in_need_for_help ) ) {
    Chain_Node     *node;
    Thread_Control *the_thread;

    node = _Chain_Get_first_unprotected( &cpu_self->Threads_in_need_for_help );
    the_thread = THREAD_OF_SCHEDULER_HELP_NODE( node );
    the_thread->Scheduler.ask_for_help_cpu = NULL;

    _Per_CPU_Release( cpu_self, &lock_context );

    _Thread_State_acquire( the_thread, &lock_context );
    _Thread_Ask_for_help( the_thread );
    _Thread_State_release( the_thread, &lock_context );

    _Per_CPU_Acquire( cpu_self, &lock_context );
  }

  _Per_CPU_Release( cpu_self, &lock_context );
#else
  (void) cpu_self;
#endif

  return level;
}

static void _Thread_Post_switch_cleanup( Thread_Control *executing )
{
#if defined(RTEMS_SMP)
  Chain_Node       *node;
  const Chain_Node *tail;

  if ( !_Thread_Can_ask_for_help( executing ) ) {
    return;
  }

  node = _Chain_First( &executing->Scheduler.Scheduler_nodes );
  tail = _Chain_Immutable_tail( &executing->Scheduler.Scheduler_nodes );

  do {
    Scheduler_Node          *scheduler_node;
    const Scheduler_Control *scheduler;
    ISR_lock_Context         lock_context;

    scheduler_node = SCHEDULER_NODE_OF_THREAD_SCHEDULER_NODE( node );
    scheduler = _Scheduler_Node_get_scheduler( scheduler_node );

    _Scheduler_Acquire_critical( scheduler, &lock_context );
    ( *scheduler->Operations.reconsider_help_request )(
      scheduler,
      executing,
      scheduler_node
    );
    _Scheduler_Release_critical( scheduler, &lock_context );

    node = _Chain_Next( node );
  } while ( node != tail );
#else
  (void) executing;
#endif
}

static Thread_Action *_Thread_Get_post_switch_action(
  Thread_Control *executing
)
{
  Chain_Control *chain = &executing->Post_switch_actions.Chain;

  return (Thread_Action *) _Chain_Get_unprotected( chain );
}

static void _Thread_Run_post_switch_actions( Thread_Control *executing )
{
  ISR_lock_Context  lock_context;
  Thread_Action    *action;

  _Thread_State_acquire( executing, &lock_context );
  _Thread_Post_switch_cleanup( executing );
  action = _Thread_Get_post_switch_action( executing );

  while ( action != NULL ) {
    _Chain_Set_off_chain( &action->Node );
    ( *action->handler )( executing, action, &lock_context );
    action = _Thread_Get_post_switch_action( executing );
  }

  _Thread_State_release( executing, &lock_context );
}

//===================================
// debug (for counting schedule count)
int c = 0;  

void _Thread_Do_dispatch( Per_CPU_Control *cpu_self, ISR_Level level )
{
  Thread_Control *executing;

  _Assert( cpu_self->thread_dispatch_disable_level == 1 );

#if defined(RTEMS_SCORE_ROBUST_THREAD_DISPATCH)
  if (
    !_ISR_Is_enabled( level )
#if defined(RTEMS_SMP) && CPU_ENABLE_ROBUST_THREAD_DISPATCH == FALSE
      && _SMP_Need_inter_processor_interrupts()
#endif
  ) {
    _Internal_error( INTERNAL_ERROR_BAD_THREAD_DISPATCH_ENVIRONMENT );
  }
#endif

  executing = cpu_self->executing;

  do {
    Thread_Control                     *heir;
    const Thread_CPU_budget_operations *cpu_budget_operations;

    level = _Thread_Preemption_intervention( executing, cpu_self, level );

    /*======================================================================================================*/
    heir = cpu_self->heir;

    // 1. Retrieve information about the current and next threads and time 
    rtems_tcb* cur_tcb =  (rtems_tcb*)executing; // currently running task
    rtems_tcb* next_tcb = (rtems_tcb*)heir;
    rtems_id id = edf_extension_id;

    uint64_t idx = rtems_object_id_get_index(id);
    edf_thread_data* cur_data = cur_tcb->extensions[idx];
    edf_thread_data* next_data = next_tcb->extensions[idx];
    uint64_t cur_time = (uint64_t)rtems_clock_get_ticks_since_boot();

    // 2. Update currenst task's remain time
    if(cur_data && cur_data->isAdvanced && cur_tcb->current_state != STATES_WAITING_FOR_PERIOD){
      // test
      // printf("===========================================================\n");
      // printf("cur time: %lld \n", cur_time);
      // printf("cur_lastest scheduled time: %lld \n", cur_data->latest_scheduled_time);
      // printf("cur remain_time: %lld \n", cur_data->remain_time);
      // printf("============================================================\n");

      uint64_t work_time = cur_time - cur_data->latest_scheduled_time;

      if(cur_data->remain_time > work_time){
        cur_data->remain_time -= work_time;
      } else{
        cur_data->remain_time = 0;
      }
      cur_data->latest_scheduled_time = cur_time;
    }

  // 3. Check if boosting is required
    if(cur_tcb->current_state != STATES_WAITING_FOR_PERIOD && (cur_data && next_data) && cur_data->isAdvanced && next_data->isAdvanced && cur_tcb != next_tcb){

      // // test 
      // printf("=======================================\n");
      // printf("cur_time: %lld \n", cur_time);
      // printf("cur remain_time %lld, ", cur_data->remain_time);
      // printf("cur deadline: %lld \n", cur_data->dead_time);
      // printf("next remain_time %lld, ", next_data->remain_time);
      // printf("next deadline %lld \n", next_data->dead_time);
      // printf("=======================================\n");


      uint64_t cur_fin_time = cur_time + cur_data->remain_time;

      // Boosting when next task's Least scheduled time is after current task's finish time(100 is guard time)
      bool isBoost = (cur_data->remain_time != 0) && (cur_fin_time < (next_data->dead_time - next_data->remain_time));

      if(isBoost){
        // Set no need to dispatch to exit while loop(Originally, setting in _Thread_Get_heir_and_make_it_executing())
        cpu_self->dispatch_necessary = false;
        printf("boosting\n");
        break; // BOOSTING
      }
    }

    // If no boosting occurs, the next task (heir) will be executed,
    // so update its latest scheduled time.
    if(next_data && next_data->isAdvanced){
      next_data->latest_scheduled_time = cur_time;

      // test 
      // printf("-------------dddd--------------------------------------\n");
      // printf("time: %lld \n", cur_time);
      // printf("next remain time: %lld \n", next_data->remain_time);
      // printf("next deadline: %lld \n", next_data->dead_time);
      // printf("-------------------------------------------------------\n");
    }
    /*======================================================================================================*/

    // Boosting Logic must be executed before this function
    heir = _Thread_Get_heir_and_make_it_executing( cpu_self );// set heir to executing.

    /*
     * If the heir and executing are the same, then there is no need to do a
     * context switch.  Proceed to run the post switch actions.  This is
     * normally done to dispatch signals.
     */
    if ( heir == executing ) {
      break;
    }

    /*
     *  Since heir and executing are not the same, we need to do a real
     *  context switch.
     */

    cpu_budget_operations = heir->CPU_budget.operations;

    if ( cpu_budget_operations != NULL ) {
      ( *cpu_budget_operations->at_context_switch )( heir );
    }

    _ISR_Local_enable( level );

#if !defined(RTEMS_SMP)
    _User_extensions_Thread_switch( executing, heir );
#endif
    //=================
    printf("count: %d \n", ++c);
    _Thread_Save_fp( executing );
    _Context_Switch( &executing->Registers, &heir->Registers );
    _Thread_Restore_fp( executing );
#if defined(RTEMS_SMP)
    _User_extensions_Thread_switch( NULL, executing );
#endif

    /*
     * We have to obtain this value again after the context switch since the
     * heir thread may have migrated from another processor.  Values from the
     * stack or non-volatile registers reflect the old execution environment.
     */
    cpu_self = _Per_CPU_Get();

    _ISR_Local_disable( level );
  } while ( cpu_self->dispatch_necessary );

  /*
   * We are done with context switching.  Proceed to run the post switch
   * actions.
   */

  _Assert( cpu_self->thread_dispatch_disable_level == 1 );
  cpu_self->thread_dispatch_disable_level = 0;
  _Profiling_Thread_dispatch_enable( cpu_self, 0 );

  _ISR_Local_enable( level );

  _Thread_Run_post_switch_actions( executing );
}

void _Thread_Dispatch_direct( Per_CPU_Control *cpu_self )
{
  ISR_Level level;

  if ( cpu_self->thread_dispatch_disable_level != 1 ) {
    _Internal_error( INTERNAL_ERROR_BAD_THREAD_DISPATCH_DISABLE_LEVEL );
  }

  _ISR_Local_disable( level );
  _Thread_Do_dispatch( cpu_self, level );
}

RTEMS_ALIAS( _Thread_Dispatch_direct ) void
_Thread_Dispatch_direct_no_return( Per_CPU_Control * );

void _Thread_Dispatch_enable( Per_CPU_Control *cpu_self )
{
  uint32_t disable_level = cpu_self->thread_dispatch_disable_level;

  if ( disable_level == 1 ) {
    ISR_Level level;

    _ISR_Local_disable( level );

    if (
      cpu_self->dispatch_necessary
#if defined(RTEMS_SCORE_ROBUST_THREAD_DISPATCH)
        || !_ISR_Is_enabled( level )
#endif
    ) {
      _Thread_Do_dispatch( cpu_self, level );
    } else {
      cpu_self->thread_dispatch_disable_level = 0;
      _Profiling_Thread_dispatch_enable( cpu_self, 0 );
      _ISR_Local_enable( level );
    }
  } else {
    _Assert( disable_level > 0 );
    cpu_self->thread_dispatch_disable_level = disable_level - 1;
  }
}