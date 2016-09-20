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

#ifndef __RAPH_KERNEL_FUNCTIONAL_H__
#define __RAPH_KERNEL_FUNCTIONAL_H__

#include <spinlock.h>
#include <task.h>
#include <functional.h>
#include <raph.h>

template<class L>
class FunctionalBase {
 public:
  enum class FunctionState {
    kFunctioning,
    kNotFunctioning,
  };
  FunctionalBase() {
    Function2<Task> func;
    func.Init(Handle, reinterpret_cast<void *>(this));
    _task.SetFunc(func);
  }
  virtual ~FunctionalBase() {
  }
  void SetFunction(int cpuid, const GenericFunction2<Task> &func);
 protected:
  void WakeupFunction();
  // check whether Functional needs to process function
  virtual bool ShouldFunc() = 0;
 private:
  static void Handle(Task *, void *p);
  FunctionBase2<Task> _func;
  Task _task;
  int _cpuid = 0;
  L _lock;
  FunctionState _state = FunctionState::kNotFunctioning;
};

template<class L>
void FunctionalBase<L>::WakeupFunction() {
  if (!_func.CanExecute()) {
    return;
  }
  Locker locker(_lock);
  if (_state == FunctionState::kFunctioning) {
    return;
  }
  _state = FunctionState::kFunctioning;
  task_ctrl->Register(_cpuid, &_task);
}

template<class L>
void FunctionalBase<L>::Handle(Task *t, void *p) {
  FunctionalBase<L> *that = reinterpret_cast<FunctionalBase<L> *>(p);
  if (that->ShouldFunc()) {
    that->_func.Execute(t);
  }
  {
    Locker locker(that->_lock);
    if (!that->ShouldFunc()) {
      that->_state = FunctionState::kNotFunctioning;
      return;
    }
  }
  task_ctrl->Register(that->_cpuid, &that->_task);
}

template<class L>
void FunctionalBase<L>::SetFunction(int cpuid, const GenericFunction2<Task> &func) {
  kassert(!_func.CanExecute());
  _cpuid = cpuid;
  _func.Copy(func);
}

using Functional = FunctionalBase<SpinLock>;

using IntFunctional = FunctionalBase<IntSpinLock>;

#endif // __RAPH_KERNEL_FUNCTIONAL_H__
