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

#include <spinlock.h>
#include <raph.h>
#include <cpu.h>
#include <libglobal.h>

#ifdef __KERNEL__
#include <idt.h>
#include <apic.h>
#endif // __KERNEL__

void IntSpinLock::Lock() {
  if ((_flag % 2) == 1) {
    kassert(_id != cpu_ctrl->GetId());
  }
  volatile unsigned int flag = GetFlag();
  while(true) {
    if ((flag % 2) != 1) {
      bool iflag = this->DisableInt();
      if (SetFlag(flag, flag + 1)) {
        _did_stop_interrupt = iflag;
        break;
      }
      this->EnableInt(iflag);
    }
    flag = GetFlag();
  }
  _id = cpu_ctrl->GetId();
}

void IntSpinLock::Unlock() {
  kassert((_flag % 2) == 1);
  _id = -1;
  this->EnableInt(_did_stop_interrupt);
  _flag++;
}

int IntSpinLock::Trylock() {
  volatile unsigned int flag = GetFlag();
  bool iflag = this->DisableInt();
  if (((flag % 2) == 0) && SetFlag(flag, flag + 1)) {
    _did_stop_interrupt = iflag;
    return 0;
  } else {
    this->EnableInt(iflag);
    return -1;
  }
}

