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

#ifndef __RAPH_LIB_CPU_H__
#define __RAPH_LIB_CPU_H__
enum class CpuPurpose {
  kNone = 0,
  kLowPriority,
  kGeneralPurpose,
  kHighPerformance,
  kCpuPurposesNum
};

class CpuId {
  private:
    int rawid;
    void Init(int newid) {
      rawid = newid;
    }
  public:
    static const int kCpuIdNotFound = -1;
    static const int kCpuIdBootProcessor = 0;
    CpuId() {
      Init(kCpuIdNotFound);
    }
    CpuId(int newid) {
      Init(newid);
    }
    int GetRawId() {
      return rawid;
    }
    uint8_t GetApicId();
    bool IsValid();
};

class CpuCtrlInterface {
  public:
    const int kCpuIdNotFound = -1;
    const int kCpuIdBootProcessor = 0;
    //
    virtual ~CpuCtrlInterface() {
    }
    virtual CpuId GetCpuId() = 0;
    virtual int GetHowManyCpus() = 0;
    virtual CpuId RetainCpuIdForPurpose(CpuPurpose p) = 0;
    virtual void ReleaseCpuId(CpuId cpuid) = 0;
    virtual void AssignCpusNotAssignedToGeneralPurpose() = 0;
};

#ifdef __KERNEL__
#include <global.h>

class CpuCtrl : public CpuCtrlInterface {
  public:
    CpuCtrl() {
      cpu_purpose_map[0] = CpuPurpose::kLowPriority;
    }
    CpuId GetCpuId() override;
    int GetHowManyCpus() override;
    CpuId RetainCpuIdForPurpose(CpuPurpose p) override; 
    void ReleaseCpuId(CpuId cpuid) override {
      int raw_cpu_id = cpuid.GetRawId();
      if(cpu_purpose_count[raw_cpu_id] > 0) cpu_purpose_count[raw_cpu_id]--;
      if(cpu_purpose_count[raw_cpu_id] == 0) {
        cpu_purpose_map[raw_cpu_id] = CpuPurpose::kNone;
      }
    }
    void AssignCpusNotAssignedToGeneralPurpose() {
      int len = GetHowManyCpus();
      for(int i = 0; i < len; i++) {
        if(cpu_purpose_map[i] == CpuPurpose::kNone) {
          RetainCpuId(i, CpuPurpose::kGeneralPurpose);
        }
      }
    }
  private:
    static CpuPurpose cpu_purpose_map[];
    static int cpu_purpose_count[];
    // do not count for cpuid:0 (boot processor is always assigned to kLowPriority)
    int GetCpuIdNotAssigned(){
      int len = GetHowManyCpus();
      for(int i = 0; i < len; i++){
        if(cpu_purpose_map[i] == CpuPurpose::kNone) return i;
      }
      return kCpuIdNotFound;
    }
    int GetCpuIdLessAssignedFor(CpuPurpose p) {
      int minCount = -1, minId = kCpuIdNotFound;
      int len = GetHowManyCpus();
      for(int i = 0; i < len; i++) {
        if(cpu_purpose_map[i] == p &&
            (minCount == -1 || cpu_purpose_count[i] < minCount)){
          minCount = cpu_purpose_count[i];
          minId = i;
        }
      }
      return minId;
    }
    void RetainCpuId(int cpuid, CpuPurpose p) {
      if(cpu_purpose_map[cpuid] != p) {
        cpu_purpose_map[cpuid] = p;
        cpu_purpose_count[cpuid] = 0;
      }
      cpu_purpose_count[cpuid]++;
    }
};

#else
#include <thread.h>
#endif /* __KERNEL__ */

#endif /* __RAPH_LIB_CPU_H__ */
