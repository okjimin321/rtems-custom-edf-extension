#ifndef EDF_EXTENSION_H_
#define EDF_EXTENSION_H_

#include<stdint.h>
#include<stdbool.h>

extern rtems_id edf_extension_id;

typedef struct{

    // edf 개선 버젼 사용 여부.
    bool isAdvanced;
    // 선점 지연을 구현하기 위한 변수.
    uint64_t remain_time;
    uint64_t execution_time;
    uint64_t latest_scheduled_time;

    uint64_t dead_time;
} edf_thread_data;


bool edf_extension_create(rtems_tcb* executting, rtems_tcb* created);
void edf_extension_delete(rtems_tcb* executing, rtems_tcb* deleted);
void initialize_edf_extension(void);


void edf_set_execution_time(rtems_tcb* cur, uint64_t execution_t, uint64_t dead_time);
void edf_start_period(rtems_tcb* cur, uint64_t deadl);
#endif