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
 * Author: hikalium
 * 
 */

#ifdef __KERNEL__
#include <apic.h>
#include <global.h>

//
// CpuId
//

uint8_t CpuId::GetApicId(){
  return apic_ctrl->GetApicIdFromCpuId(id);
}
bool CpuId::IsValid(){
  return (id >= 0 && id < cpu_ctrl->GetHowManyCpus());
}

//
// CpuCtrl
//

inline CpuId CpuCtrl::GetCpuId()
{
  return apic_ctrl->GetCpuId();
}
inline int CpuCtrl::GetHowManyCpus()
{
  return apic_ctrl->GetHowManyCpus();
}

CpuPurpose CpuCtrl::cpu_purpose_map[ApicCtrl::lapicMaxNumber];
int CpuCtrl::cpu_purpose_count[ApicCtrl::lapicMaxNumber];


#else
#include <thread.h>
#endif /* __KERNEL__ */

