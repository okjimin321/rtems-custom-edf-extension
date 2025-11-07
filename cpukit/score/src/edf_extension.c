#include <rtems.h>
#include <rtems/score/thread.h>
#include <stdlib.h>
#include <rtems/edf_extension.h>
#include<stdio.h>

rtems_id edf_extension_id;

// Create EDF extension for a newly created thread
bool edf_extension_create(rtems_tcb* executing, rtems_tcb* created){

    uint64_t idx = rtems_object_id_get_index(edf_extension_id);

    // 1. Allocate memory for Advaneced Edf-scheduling(Boosting) data
    edf_thread_data *data = malloc(sizeof(edf_thread_data));
    if(data == NULL){
        return false;
    }

    // 2. Initialize fields to default values
    data->isAdvanced = false;
    data->execution_time = 0;
    data->remain_time = 0;
    data->latest_scheduled_time = 0;

    created->extensions[idx] = data;

    return true;
}

// Delete EDF extension and free associated memory
void edf_extension_delete(rtems_tcb* executing, rtems_tcb* deleted){

    uint64_t idx = rtems_object_id_get_index(edf_extension_id);

    edf_thread_data * data = deleted->extensions[idx];

    if(data != NULL)
        free(data);

    deleted->extensions[idx] = NULL;
}

// Initialize the EDF scheduling extension in RTEMS
void initialize_edf_extension(void){
    rtems_extensions_table edf_extension_table = {
        edf_extension_create,
        NULL,
        NULL,
        edf_extension_delete,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
    };

    rtems_status_code status = rtems_extension_create(
        rtems_build_name('e', 'd', 'f', 'x'),
        &edf_extension_table,
        &edf_extension_id
    );

    if(status != RTEMS_SUCCESSFUL)
        printf("Failed to create EDF extension: %s\n", rtems_status_text(status));
}

// Initialize EDF parameters for a thread, enabling EDF-based scheduling
void edf_set_execution_time(rtems_tcb* cur, uint64_t execution_t, uint64_t deadline){
    uint64_t idx = rtems_object_id_get_index(edf_extension_id);

    edf_thread_data * cur_data = cur->extensions[idx];

    // set execution time
    cur_data->isAdvanced = true;
    cur_data->execution_time = execution_t;
    cur_data->remain_time = execution_t;
    cur_data->latest_scheduled_time = 0;

    cur_data->dead_time = deadline;
}

// Reset execution and timing parameters for the new period
void edf_start_period(rtems_tcb* cur, uint64_t deadline){
    uint64_t idx = rtems_object_id_get_index(edf_extension_id);
    edf_thread_data * cur_data = cur->extensions[idx];

    cur_data->remain_time = cur_data->execution_time;
    cur_data->dead_time = deadline;
    cur_data->latest_scheduled_time = 0;
}