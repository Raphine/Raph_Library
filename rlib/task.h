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

#include "_task.h"
#include "_queue.h"

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
  void Register(int cpuid, Task *task);
  void Remove(Task *task);
  void Wait();
  void Run();
  TaskQueueState GetState(int cpuid) {
    if (_task_struct == nullptr) {
      return TaskQueueState::kNotStarted;
    }
    return _task_struct[cpuid].state;
  }
 private:
  friend Callout;
  void RegisterCallout(Callout *task);
  void CancelCallout(Callout *task);
  void ForceWakeup(int cpuid);
  class TaskThread : public IntQueue<TaskThread>::ContainerInterface {
  public:
    TaskThread(const bool allocate);
    virtual ~TaskThread() {
    }
    virtual bool Enable();
  private:
    TaskThread();
    virtual IntQueue<TaskThread>::Container *GetIntQueueContainer() override {
      return &_container;
    }
    IntQueue<TaskThread>::Container _container;
    const bool _allocated;
  };
  void RunSub(TaskThread *thread);
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

    IntQueue<TaskThread> waiting_thread;
    IntQueue<TaskThread> running_thread;
  } *_task_struct = nullptr;
  // this const value defines interval of wakeup task controller when all task slept
  // (task controller doesn't sleep if there is any registered tasks)
  static const int kTaskExecutionInterval = 1000; // us
};

#endif /* __RAPH_LIB_TASK_H__ */
