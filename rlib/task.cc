/*
 *
 * Copyright (c) 2016 Raphine Project
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Author: Liva, hikalium
 * 
 */

#include <task.h>
#include <cpu.h>

#ifdef __KERNEL__
#include <apic.h>
#else
#include <raph.h>
#include <unistd.h>
#endif // __KERNEL__

void TaskCtrl::Setup() {
  int cpus = cpu_ctrl->GetHowManyCpus();
  _task_struct = reinterpret_cast<TaskStruct *>(virtmem_ctrl->Alloc(sizeof(TaskStruct) * cpus));
  Task *t;
  for (int i = 0; i < cpus; i++) {
    new(&_task_struct[i]) TaskStruct;

    t = virtmem_ctrl->New<Task>();
    t->_status = Task::Status::kGuard;
    t->_next = nullptr;
    t->_prev = nullptr;

    _task_struct[i].top = t;
    _task_struct[i].bottom = t;

    t = virtmem_ctrl->New<Task>();
    t->_status = Task::Status::kGuard;
    t->_next = nullptr;
    t->_prev = nullptr;

    _task_struct[i].top_sub = t;
    _task_struct[i].bottom_sub = t;

    _task_struct[i].state = TaskQueueState::kNotStarted;

    Callout *dt = virtmem_ctrl->New<Callout>();
    dt->_next = nullptr;
    _task_struct[i].dtop = dt;
  }
}

void TaskCtrl::Run() {
  CpuId cpuid = cpu_ctrl->GetCpuId();
  int raw_cpu_id = cpuid.GetRawId();
  TaskStruct *ts = &_task_struct[raw_cpu_id];

  ts->state = TaskQueueState::kNotRunning;
#ifdef __KERNEL__
  apic_ctrl->SetupTimer(kTaskExecutionInterval);
#endif // __KERNEL__
  while(true) {
    TaskQueueState oldstate;
    {
      Locker locker(ts->lock);
      oldstate = ts->state;
#ifdef __KERNEL__
      if (oldstate == TaskQueueState::kNotRunning) {
        apic_ctrl->StopTimer();
      }
#endif // __KERNEL__
      kassert(oldstate == TaskQueueState::kNotRunning
              || oldstate == TaskQueueState::kSlept);
      ts->state = TaskQueueState::kRunning;
    }
    if (oldstate == TaskQueueState::kNotRunning) {
      uint64_t time = timer->GetCntAfterPeriod(timer->ReadMainCnt(), kTaskExecutionInterval);
      
      Callout *dt = ts->dtop;
      while(true) {
        Callout *dtt;
        {
          Locker locker(ts->dlock);
          dtt = dt->_next;
          if (dtt == nullptr) {
            break;
          }
          if (timer->IsGreater(dtt->_time, time)) {
            break;
          }
          if (dtt->_lock.Trylock() < 0) {
            // retry
            continue;
          }
          dt->_next = dtt->_next;
        }
        dtt->_next = nullptr;
        dtt->_state = Callout::CalloutState::kTaskQueue;
        Register(cpuid, &dtt->_task);
        dtt->_lock.Unlock();
        break;
      }
    }
    while(true) {
      while(true) {
        Task *t;
        {
          Locker locker(ts->lock);
          Task *tt = ts->top;
          t = tt->_next;
          if (t == nullptr) {
            kassert(tt == ts->bottom);
            break;
          }
          tt->_next = t->_next;
          if (t->_next == nullptr) {
            kassert(ts->bottom == t);
            ts->bottom = tt;
          } else {
            t->_next->_prev = tt;
          }
          kassert(t->_status == Task::Status::kWaitingInQueue);
          t->_status = Task::Status::kRunning;
          t->_next = nullptr;
          t->_prev = nullptr;
        }
        t->Execute();

        {
          Locker locker(ts->lock);
          if (t->_status == Task::Status::kRunning) {
            t->_status = Task::Status::kOutOfQueue;
          }
        }
      }
      Locker locker(ts->lock);

      if (ts->top->_next == nullptr &&
          ts->top_sub->_next == nullptr) {
        ts->state = TaskQueueState::kSlept;
        break;
      }
      Task *tmp;
      tmp = ts->top;
      ts->top = ts->top_sub;
      ts->top_sub = tmp;

      tmp = ts->bottom;
      ts->bottom = ts->bottom_sub;
      ts->bottom_sub = tmp;

      //TODO : FIX THIS : callout isn't executed while this loop is running.
    }
    
    kassert(ts->state == TaskQueueState::kSlept);

    {
      Locker locker(ts->dlock);
      if (ts->dtop->_next != nullptr) {
        ts->state = TaskQueueState::kNotRunning;
      }
    }
#ifdef __KERNEL__
    if (ts->state == TaskQueueState::kNotRunning) {
      apic_ctrl->StartTimer();
    } else {
      kassert(ts->state == TaskQueueState::kSlept);
    }
    asm volatile("hlt");
#else
    usleep(10);
#endif // __KERNEL__
  }
}

void TaskCtrl::Register(CpuId cpuid, Task *task) {
  if (!cpuid.IsValid()) {
    return;
  }
  int raw_cpu_id = cpuid.GetRawId();
  Locker locker(_task_struct[raw_cpu_id].lock);
  if (task->_status == Task::Status::kWaitingInQueue) {
    return;
  }
  task->_cpuid = raw_cpu_id;
  task->_next = nullptr;
  task->_status = Task::Status::kWaitingInQueue;
  _task_struct[raw_cpu_id].bottom_sub->_next = task;
  task->_prev = _task_struct[raw_cpu_id].bottom_sub;
  _task_struct[raw_cpu_id].bottom_sub = task;
  
  ForceWakeup(raw_cpu_id);
}

void TaskCtrl::Remove(Task *task) {
  kassert(task->_status != Task::Status::kGuard);
  int cpuid = task->_cpuid;
  Locker locker(_task_struct[cpuid].lock);
  switch(task->_status) {
  case Task::Status::kWaitingInQueue: {
    Task *next = task->_next;
    Task *prev = task->_prev;

    task->_next = nullptr;
    task->_prev = nullptr;

    kassert(prev != nullptr);
    prev->_next = next;

    if (next == nullptr) {
      if (task == _task_struct[cpuid].bottom) {
        _task_struct[cpuid].bottom = prev;
      } else if (task == _task_struct[cpuid].bottom_sub) {
        _task_struct[cpuid].bottom_sub = prev;
      } else {
        kassert(false);
      }
    } else {
      next->_prev = prev;
    }

    prev->_next = next;
    break;
  }
  case Task::Status::kRunning:
  case Task::Status::kOutOfQueue: {
    break;
  }
  default:{
    kassert(false);
  }
  }
  task->_status = Task::Status::kOutOfQueue;  
}

void TaskCtrl::RegisterCallout(Callout *task) {
  CpuId cpuid = task->_cpuid;
  TaskStruct *ts = &_task_struct[task->_cpuid];
  if (cpuid.IsValid()) {
    return;
  }
  {
    Locker locker(ts->dlock);
    Callout *dt = ts->dtop;
    while(true) {
      Callout *dtt = dt->_next;
      if (dt->_next != nullptr) {
        task->_state = Callout::CalloutState::kCalloutQueue;
      	task->_next = dtt;
      	dt->_next = task;
      	break;
      }
      if (timer->IsGreater(dtt->_time, task->_time)) {
        task->_state = Callout::CalloutState::kCalloutQueue;
        task->_next = dtt;
        dt->_next = task;
        break;
      }
      dt = dtt;
    }
  }

  ForceWakeup(cpuid);
}

void TaskCtrl::CancelCallout(Callout *task) {
  int cpuid = task->_cpuid;
  switch(task->_state) {
  case Callout::CalloutState::kCalloutQueue: {
    Locker locker(_task_struct[cpuid].dlock);
    Callout *dt = _task_struct[cpuid].dtop;
    while(dt->_next != nullptr) {
      Callout *dtt = dt->_next;
      if (dtt == task) {
        dt->_next = dtt->_next;
        break;
      }
      dt = dtt;
    }
    task->_next = nullptr;
    break;
  }
  case Callout::CalloutState::kTaskQueue: {
    Remove(&task->_task);
    break;
  }
  case Callout::CalloutState::kHandling:
  case Callout::CalloutState::kStopped: {
    break;
  }
  default:
    kassert(false);
  }
  task->_state = Callout::CalloutState::kStopped;
}

void TaskCtrl::ForceWakeup(CpuId cpuid) {
#ifdef __KERNEL__
  if (_task_struct[cpuid.GetRawId()].state == TaskQueueState::kSlept) {
    if (cpu_ctrl->GetCpuId().GetRawId() != cpuid.GetRawId()) {
      apic_ctrl->SendIpi(cpuid.GetApicId());
    }
  }
#endif // __KERNEL__
}

Task::~Task() {
  kassert(_status == Status::kOutOfQueue);
}

void CountableTask::Inc() {
  CpuId cpuid(_cpuid);
  if (!cpuid.IsValid()) {
    return;
  }
  //TODO CASを使って高速化
  Locker locker(_lock);
  _cnt++;
  if (_cnt == 1) {
    task_ctrl->Register(cpuid, &_task);
  }
}

void CountableTask::HandleSub(void *) {
  _func.Execute();
  {
    Locker locker(_lock);
    _cnt--;
    if (_cnt != 0) {
      task_ctrl->Register(_cpuid, &_task);
    }
  }
}

void Callout::SetHandler(uint32_t us) {
  SetHandler(cpu_ctrl->GetCpuId(), us);
}

void Callout::SetHandler(CpuId cpuid, int us) {
  Locker locker(_lock);
  _time = timer->GetCntAfterPeriod(timer->ReadMainCnt(), us);
  _cpuid = cpuid.GetRawId();
  task_ctrl->RegisterCallout(this);
}

void Callout::Cancel() {
  Locker locker(_lock);
  task_ctrl->CancelCallout(this);
}

void Callout::HandleSub(void *) {
  if (timer->IsTimePassed(_time)) {
    _state = CalloutState::kHandling;
    _func.Execute();
    _state = CalloutState::kStopped;
  } else {
    task_ctrl->Register(cpu_ctrl->GetCpuId(), &_task);
  }
}
