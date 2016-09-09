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

#ifndef __RAPH_LIB_CPU_H__
#define __RAPH_LIB_CPU_H__

class CpuCtrlInterface {
public:
  enum CpuPurposes {
    kNone = 0,
	kLowPriority,
	kGeneralPurpose,
	kHighPerformance,
	kCpuPurposesLen
  };
  const int kCpuIdNotFound = -1;
  const int kCpuIdBootProcessor = 0;
  //
  virtual ~CpuCtrlInterface() {
  }
  virtual volatile int GetId() = 0;
  virtual int GetHowManyCpus() = 0;
  virtual int RetainCpuIdForPurpose(CpuPurposes p) = 0;
  virtual void ReleaseCpuId(int cpuid) = 0;
  bool IsValidId(int cpuid) {
    return (cpuid >= 0 && cpuid < GetHowManyCpus());
  }
};

#ifdef __KERNEL__
#include <apic.h>
#include <global.h>

class CpuCtrl : public CpuCtrlInterface {
public:
  CpuCtrl() {
    _cpu_purpose_map[0] = kLowPriority;
  }
  volatile int GetId() override {
    return apic_ctrl->GetCpuId();
  }
  int GetHowManyCpus() override {
    return apic_ctrl->GetHowManyCpus();
  }
  int RetainCpuIdForPurpose(CpuPurposes p) override {
    if(p == kLowPriority) return kCpuIdBootProcessor;
      // boot processor is always assigned to kLowPriority
    int cpuid;
    cpuid = _GetCpuIdNotAssigned();
    if(cpuid != kCpuIdNotFound){
      _RetainCpuId(cpuid, p);
      return cpuid;
    }
    cpuid = _GetCpuIdLessAssignedFor(p);
    if(cpuid != kCpuIdNotFound){
      _RetainCpuId(cpuid, p);
      return cpuid;
    }
    return 0;
  }
  void ReleaseCpuId(int cpuid) override {
    _ReleaseCpuId(cpuid);
  }
private:
  CpuPurposes _cpu_purpose_map[ApicCtrl::lapicMaxNumber];
  int _cpu_purpose_count[ApicCtrl::lapicMaxNumber];
  // do not count for cpuid:0 (boot processor is always assigned to kLowPriority)
  int _GetCpuIdNotAssigned(){
    int i, len;
    len = GetHowManyCpus();
    for(i = 0; i < len; i++){
      if(_cpu_purpose_map[i] == kNone) return i;
    }
    return kCpuIdNotFound;
  }
  int _GetCpuIdLessAssignedFor(CpuPurposes p){
    int i, len, minCount = -1, minId = kCpuIdNotFound;
    len = GetHowManyCpus();
    for(i = 0; i < len; i++){
      if(_cpu_purpose_map[i] == p &&
		(minCount == -1 || _cpu_purpose_count[i] < minCount)){
        minCount = _cpu_purpose_count[i];
		minId = i;
      }
    }
    return minId;
  }
  void _RetainCpuId(int cpuid, CpuPurposes p){
    if(_cpu_purpose_map[cpuid] != p){
      _cpu_purpose_map[cpuid] = p;
      _cpu_purpose_count[cpuid] = 0;
    }
    _cpu_purpose_count[cpuid]++;
  }
  void _ReleaseCpuId(int cpuid){
    if(_cpu_purpose_count[cpuid] > 0) _cpu_purpose_count[cpuid]--;
    if(_cpu_purpose_count[cpuid] == 0){
      _cpu_purpose_map[cpuid] = kNone;
    }
  }
};

#else
#include <thread.h>
#endif /* __KERNEL__ */

#endif /* __RAPH_LIB_CPU_H__ */
