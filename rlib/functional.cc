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

#include <functional.h>
#include <task.h>
#include <raph.h>

void Functional::WakeupFunction() {
  if (!_func.CanExecute()) {
    return;
  }
  Locker locker(GetLock());
  if (_state == FunctionState::kFunctioning) {
    return;
  }
  _state = FunctionState::kFunctioning;
  task_ctrl->Register(_cpuid, &_task);
}

void Functional::Handle(Task *t, void *p) {
  Functional *that = reinterpret_cast<Functional *>(p);
  if (that->ShouldFunc()) {
    that->_func.Execute(t);
  }
  {
    Locker locker(that->GetLock());
    if (!that->ShouldFunc()) {
      that->_state = FunctionState::kNotFunctioning;
      return;
    }
  }
  task_ctrl->Register(that->_cpuid, &that->_task);
}

void Functional::SetFunction(int cpuid, const GenericFunction2<Task> &func) {
  kassert(!_func.CanExecute());
  _cpuid = cpuid;
  _func.Copy(func);
}

SpinLockInterface &Functional::GetLock() {
  return _lock;
}

SpinLockInterface &IntFunctional::GetLock() {
  return _lock;
}
