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
enum class CpuPurpose {
  kNone = 0,
  kLowPriority,
  kGeneralPurpose,
  kHighPerformance,
  kCpuPurposesNum
};

class CpuCtrlInterface {
  public:
    const int kCpuIdNotFound = -1;
    const int kCpuIdBootProcessor = 0;
    //
    virtual ~CpuCtrlInterface() {
    }
    virtual volatile int GetCpuId() = 0;
    virtual int GetHowManyCpus() = 0;
    virtual int RetainCpuIdForPurpose(CpuPurpose p) = 0;
    virtual void ReleaseCpuId(int cpuid) = 0;
    bool IsValidCpuId(int cpuid) {
      return (cpuid >= 0 && cpuid < GetHowManyCpus());
    }
};

#ifdef __KERNEL__
#include <apic.h>
#include <global.h>

class CpuCtrl : public CpuCtrlInterface {
  public:
    CpuCtrl() {
      _cpu_purpose_map[0] = CpuPurpose::kLowPriority;
    }
    volatile int GetCpuId() override {
      return apic_ctrl->GetCpuId();
    }
    int GetHowManyCpus() override {
      return apic_ctrl->GetHowManyCpus();
    }
    int RetainCpuIdForPurpose(CpuPurpose p) override {
      // Returns valid CpuId all time.
      // boot processor is always assigned to kLowPriority
      if(p == CpuPurpose::kLowPriority) return kCpuIdBootProcessor;
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
      return kCpuIdBootProcessor;
    }
    void ReleaseCpuId(int cpuid) override {
      if(_cpu_purpose_count[cpuid] > 0) _cpu_purpose_count[cpuid]--;
      if(_cpu_purpose_count[cpuid] == 0){
        _cpu_purpose_map[cpuid] = CpuPurpose::kNone;
      }
    }
  private:
    CpuPurpose _cpu_purpose_map[ApicCtrl::lapicMaxNumber];
    int _cpu_purpose_count[ApicCtrl::lapicMaxNumber];
    // do not count for cpuid:0 (boot processor is always assigned to kLowPriority)
    int _GetCpuIdNotAssigned(){
      int len = GetHowManyCpus();
      for(int i = 0; i < len; i++){
        if(_cpu_purpose_map[i] == CpuPurpose::kNone) return i;
      }
      return kCpuIdNotFound;
    }
    int _GetCpuIdLessAssignedFor(CpuPurpose p){
      int minCount = -1, minId = kCpuIdNotFound;
      int len = GetHowManyCpus();
      for(int i = 0; i < len; i++){
        if(_cpu_purpose_map[i] == p &&
            (minCount == -1 || _cpu_purpose_count[i] < minCount)){
          minCount = _cpu_purpose_count[i];
          minId = i;
        }
      }
      return minId;
    }
    void _RetainCpuId(int cpuid, CpuPurpose p){
      if(_cpu_purpose_map[cpuid] != p){
        _cpu_purpose_map[cpuid] = p;
        _cpu_purpose_count[cpuid] = 0;
      }
      _cpu_purpose_count[cpuid]++;
    }
};

#else
#include <thread.h>
#endif /* __KERNEL__ */

#endif /* __RAPH_LIB_CPU_H__ */
