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
 * Author: Liva
 * 
 */

#ifndef __RAPH_LIB_TASK_H__
#define __RAPH_LIB_TASK_H__

#include <global.h>
#include <function.h>
#include <spinlock.h>
#include <timer.h>
#include <cpu.h>

class Task;
class Callout;

class TaskCtrl {
public:
  enum class TaskQueueState {
    kNotStarted,
    kNotRunning,
    kRunning,
    kSlept,
  };
  TaskCtrl() {}
  void Setup();
  void Register(CpuId cpuid, Task *task);
  void Remove(Task *task);
  void Run();
  TaskQueueState GetState(CpuId cpuid) {
    if (_task_struct == nullptr) {
      return TaskQueueState::kNotStarted;
    }
    return _task_struct[cpuid.GetRawId()].state;
  }
 private:
  class ProcHaltCtrl {
  public:
    virtual ~ProcHaltCtrl() {
    }
    virtual void SetupSchedular(int interval) {
    }
    virtual void Halt() {
    }
    virtual void ScheduleWakeup() {
    }
  };
  
  friend Callout;
  void RegisterCallout(Callout *task);
  void CancelCallout(Callout *task);
  void ForceWakeup(CpuId cpuid);
  struct TaskStruct {
    // queue
    Task *top;
    Task *bottom;
    Task *top_sub;
    Task *bottom_sub;
    IntSpinLock lock;

    TaskQueueState state;

    // for Callout
    IntSpinLock dlock;
    Callout *dtop;
  } *_task_struct = nullptr;
  // this const value defines interval of wakeup task controller when all task slept
  // (task controller doesn't sleep if there is any registered tasks)
  static const int kTaskExecutionInterval = 1000; // us
};

class Task {
public:
  Task() {
  }
  virtual ~Task();
  void SetFunc(const GenericFunction &func) {
    _func.Copy(func);
  }
  enum class Status {
    kRunning,
    kWaitingInQueue,
    kOutOfQueue,
    kGuard,
  };
  Status GetStatus() {
    return _status;
  }
private:
  void Execute() {
    _func.Execute();
  }
  FunctionBase _func;
  Task *_next;
  Task *_prev;
  int _cpuid = -1;
  Status _status = Status::kOutOfQueue;
  friend TaskCtrl;
};
 
// Taskがキューに積まれている間にインクリメント可能
// 割り込み内からも呼び出せる
// ただし、一定時間後に立ち上げる事や割り当てcpuidを変える事はできない
class CountableTask {
public:
  CountableTask() {
    _cnt = 0;
    _cpuid = -1;
    ClassFunction<CountableTask> func;
    func.Init(this, &CountableTask::HandleSub, nullptr);
    _task.SetFunc(func);
  }
  virtual ~CountableTask() {
  }
  void SetFunc(CpuId cpuid, const GenericFunction &func) {
    _cpuid = cpuid.GetRawId();
    _func.Copy(func);
  }
  Task::Status GetStatus() {
    return _task.GetStatus();
  }
  void Inc();
private:
  void HandleSub(void *);
  Task _task;
  IntSpinLock _lock;
  FunctionBase _func;
  int _cnt;
  int _cpuid;
};

// 遅延実行されるタスク 
// 一度登録すると、実行されるかキャンセルするまでは再登録はできない
// 割り込み内からも呼び出し可能
class Callout {
public:
  enum class CalloutState {
    kCalloutQueue,
    kTaskQueue,
    kHandling,
    kStopped,
  };
  Callout() {
    ClassFunction<Callout> func;
    func.Init(this, &Callout::HandleSub, nullptr);
    _task.SetFunc(func);
  }
  virtual ~Callout() {
  }
  void Init(const GenericFunction &func) {
    _func.Copy(func);
  }
  volatile bool IsHandling() {
    return (_state == CalloutState::kHandling);
  }
  volatile bool CanExecute() {
    return _func.CanExecute();
  }
  void SetHandler(uint32_t us);
  void SetHandler(CpuId cpuid, int us);
  void Cancel();
private:
  void HandleSub(void *);
  int _cpuid;
  Task _task;
  uint64_t _time;
  Callout *_next;
  FunctionBase _func;
  IntSpinLock _lock;
  friend TaskCtrl;
  CalloutState _state = CalloutState::kStopped;
};

#include "spinlock.h"

class LckCallout {
 public:
  LckCallout() {
    ClassFunction<LckCallout> func;
    func.Init(this, &LckCallout::HandleSub, nullptr);
    callout.Init(func);
  }
  virtual ~LckCallout() {
  }
  void Init(const GenericFunction &func) {
    _func.Copy(func);
  }
  void SetLock(SpinLock *lock) {
    _lock = lock;
  }
  volatile bool IsHandling() {
    return callout.IsHandling();
  }
  volatile bool CanExecute() {
    return callout.CanExecute();
  }
  void SetHandler(uint32_t us) {
    callout.SetHandler(us);
  }
  void SetHandler(CpuId cpuid, int us) {
    callout.SetHandler(cpuid, us);
  }
  void Cancel() {
    callout.Cancel();
  }
 private:
  void HandleSub(void *) {
    if (_lock != nullptr) {
      _lock->Lock();
    }
    _func.Execute();
    if (_lock != nullptr) {
      _lock->Unlock();
    }
  }
  SpinLock *_lock = nullptr;
  Callout callout;
  FunctionBase _func;
};


#endif /* __RAPH_LIB_TASK_H__ */
