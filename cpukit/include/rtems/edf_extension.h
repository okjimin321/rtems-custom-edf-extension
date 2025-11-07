#ifndef EDF_EXTENSION_H_
#define EDF_EXTENSION_H_

#include<stdint.h>
#include<stdbool.h>

extern rtems_id edf_extension_id;

/**
 * @brief Data structure for EDF thread-specific scheduling metadata.
 */
typedef struct{

    // Indicates whether the enhanced EDF scheduling feature is enabled for this thread.
    bool isAdvanced;

    uint64_t remain_time;           // Remaining execution time
    uint64_t execution_time;        // Base execution time, used to initialize remain_time at period restart
    uint64_t latest_scheduled_time; // Last scheduled time, used for remain_time calculation

    uint64_t dead_time;             // Absolute deadline
} edf_thread_data;

void initialize_edf_extension(void); // Initialize the EDF scheduling extension

bool edf_extension_create(rtems_tcb* executting, rtems_tcb* created); // Allocate EDF extension data to the newly created TCB

void edf_extension_delete(rtems_tcb* executing, rtems_tcb* deleted);  // Free EDF extension data from the deleted TCB

void edf_set_execution_time(rtems_tcb* cur, uint64_t execution_t, uint64_t dead_time); // Assign EDF parameters (execution time and deadline) to the TCB

void edf_start_period(rtems_tcb* cur, uint64_t deadl); // Reset remaining time and update deadline at the start of a new period
#endif