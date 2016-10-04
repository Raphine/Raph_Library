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

#ifndef __RAPH_LIB__TASK_H__
#define __RAPH_LIB__TASK_H__

#include <setjmp.h>
#include <global.h>
#include <function.h>
#include <spinlock.h>
#include <timer.h>

class TaskThread;

class Task {
public:
  Task() {
  }
  virtual ~Task();
  void SetFunc(const GenericFunction2<Task> &func) {
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
  void Execute(jmp_buf buf);
private:
  void ExecuteSub();
  FunctionBase2<Task> _func;
  Task *_next;
  Task *_prev;
  TaskThread *_thread;
  Status _status = Status::kOutOfQueue;
  friend TaskCtrl;  // TODO should be removed
};
 
// Taskがキューに積まれている間にインクリメント可能
// 割り込み内からも呼び出せる
// ただし、一定時間後に立ち上げる事や割り当てcpuidを変える事はできない
class CountableTask {
public:
  CountableTask() {
    _cnt = 0;
    _cpuid = -1;
    ClassFunction2<CountableTask, Task> func;
    func.Init(this, &CountableTask::HandleSub, nullptr);
    _task.SetFunc(func);
  }
  virtual ~CountableTask() {
  }
  void SetFunc(int cpuid, const GenericFunction &func) {
    _cpuid = cpuid;
    _func.Copy(func);
  }
  Task::Status GetStatus() {
    return _task.GetStatus();
  }
  void Inc();
private:
  void HandleSub(Task *, void *);
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
    ClassFunction2<Callout, Task> func;
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
  void SetHandler(int cpuid, int us);
  void Cancel();
  bool IsPending() {
    return _pending;
  }
private:
  void HandleSub(Task *, void *);
  int _cpuid;
  Task _task;
  uint64_t _time;
  Callout *_next;
  FunctionBase _func;
  IntSpinLock _lock;
  bool _pending = false;
  friend TaskCtrl;
  CalloutState _state = CalloutState::kStopped;
};

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
  void SetHandler(int cpuid, int us) {
    callout.SetHandler(cpuid, us);
  }
  void Cancel() {
    callout.Cancel();
  }
  bool IsPending() {
    return callout.IsPending();
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

#endif /* __RAPH_LIB__TASK_H__ */
