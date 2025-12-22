RTEMS Temporal Boosting Scheduler Extension
==========================================
## Overview

본 프로젝트는 RTEMS(Real-Time Executive for Multiprocessor Systems)의

EDF(Earliest Deadline First) 스케줄러를 확장하여, **Temporal Boosting 기법을 통해 불필요한 선점(preemption)을 억제하고**,

그 결과 **우선순위 역전(Priority Inversion)과 마이그레이션(migration) 가능성을 완화**하는 것을 목표로 한다.

본 구현은 RTEMS의 EDF 스케줄러를 수정하여 구현하였다.

---

## Motivation

RTEMS의 EDF 스케줄러는 동적 우선순위 기반 특성상 잦은 선점이 발생할 수 있다.

특히 다음과 같은 문제가 존재한다.

- 잦은 선점으로 인한 **Context Switch 증가**
- 다중 코어 환경에서의 **마이그레이션 발생 가능성**
- 임계 구역 수행 중 선점으로 인한 **Priority Inversion 심화**

본 연구는 선점이 필수이지 않은 상황에서 발생하여, 위와 같은 문제가 생기는 것을 방지하자는 아이디어에서 출발하였다. 즉, 선점이 필수가 아닌 상황에서는 선점을 방지하여 현재 테스크의 실행시간을 보장해주는 것이다.

이를 기반으로 **Temporal Boosting** 기법을 RTEMS EDF 스케줄러에 적용하였다.

---

## Background

### RTEMS EDF Scheduler

- Priority-based, preemptive EDF scheduling
- 주기적 태스크(Periodic Task) 기반
- Monotonic Manager를 통해 주기 관리

### Problem in EDF

- 불필요한 선점 다수
- 선점 중 자원 보유 시 Priority Inversion 가능성
- MP 환경에서 캐시 무효화 및 마이그레이션 비용 증가

---

## Temporal Boosting Design

### Core Idea

현재 실행 중인 스레드의 **예상 종료 시각**이

다음 스레드가 deadline을 만족하기 위해 **최소로 시작해야 하는 시각(Least Feasible Start Time)** 보다 이르다면,

다음 스레드의 선점을 **일시적으로 지연**한다.

이를 통해:

- 현재 스레드가 임계 구역을 조기에 완료
- 자원을 빠르게 해제
- 결과적으로 Priority Inversion 완화

---

### Boosting Condition

Temporal Boosting은 다음 조건을 만족할 때만 허용된다.

- 현재 스레드의 예상 종료 시각
- 다음 스레드의 deadline 및 남은 실행 시간
- Guard Time (실험적으로 설정된 안전 여유 시간)

```
`T_cur + R_cur < D_next - R_next - G`
```

 그렇기 때문에 Guard Time은 예외 상황으로 인한 deadline miss를 방지하기 위해 도입되었다.

---

## Implementation Details

### 1. RTEMS User Extension을 통한 TCB 확장

RTEMS의 user extension 메커니즘을 활용하여 EDF 스레드에 추가 메타데이터를 저장한다.

```c
typedef struct{
bool isAdvanced;
uint64_t remain_time;
uint64_t execution_time;
uint64_t latest_scheduled_time;
uint64_t deadline;
} edf_thread_data;
```

- `remain_time`: 남은 실행 시간
- `latest_scheduled_time`: 실행 시간 추적용
- `deadline`: 절대 마감 시각
- `isAdvanced`: Temporal Boosting 적용 대상 여부

---

### 2. 실행 시간 및 주기 관리

- 태스크 생성 시 `edf_set_execution_time()`을 통해 실행 시간 및 deadline 설정
- 주기 시작 시 `edf_start_period()`를 통해 정보 갱신
- Monotonic Manager 내부에 통합하여 RTEMS 기존 구조와의 호환성 유지

---

### 3. Remaining Time Tracking

타이머 인터럽트마다 실행 시간을 감소시키는 방식 대신,

**선점 발생 시점에서만 실행 시간을 계산**하여 시스템 오버헤드를 최소화하였다.

```c
uint64_t work_time = cur_time - cur_data->latest_scheduled_time;
cur_data->remain_time -= work_time;
cur_data->latest_scheduled_time = cur_time;
```

---

### 4. Temporal Boosting Decision

`_Thread_Do_dispatch()` 내부에서 다음 스레드와 현재 스레드의 상태를 비교하여 Boosting 여부를 판단한다.

- Boosting이 허용되면 선점을 지연
- 허용되지 않으면 기존 EDF 방식 유지

---

## Evaluation

### Experimental Setup

- RTEMS Version: RTEMS-6
- Environment: SIS (SPARC / LEON3)
- Architecture: Single-core (UP)

### Test Scenario

두 개의 주기적 태스크 A, B를 사용하여 비교 실험 수행

| Task | Period | Execution Time | Arrival Time |
| --- | --- | --- | --- |
| A | 200 | 30 | 0 |
| B | 100 | 32 | 10 |

---

### Results

Temporal Boosting 적용 시:

- Context Switch 수 **약 16.7% 감소**
- Deadline miss 발생 없음
- 불필요한 선점 감소 확인

이는 스케줄링 오버헤드 감소와 실시간성 유지가 동시에 가능함을 보여준다.

---

## Analysis

Temporal Boosting은 EDF의 동적 특성을 유지하면서도,

- 불필요한 선점을 억제
- 임계 구역 실행 연장
- Priority Inversion 완화

라는 효과를 가진다.

특히 MP 환경에서는 마이그레이션 감소 효과로 이어질 가능성이 크다.

---

## Limitations & Future Work

- 본 실험은 단일 코어(UP) 환경에서 수행됨
- SMP 환경에서의 실제 마이그레이션 감소 효과는 추가 검증 필요
- Guard Time은 workload 특성에 따라 튜닝 필요

향후 연구로 SMP RTEMS 환경에서의 정량적 분석을 계획하고 있다.

---

## Conclusion

본 프로젝트는 RTEMS 커널 스케줄러에 Temporal Boosting 기법을 직접 적용하여,

EDF 스케줄러의 구조적 한계를 완화할 수 있음을 실험적으로 검증하였다.

---

## References

논문 참고문헌과 동일

## Full Paper 

([Full Paper](<RTEMS Temporal Boosting Scheduler.pdf>))
