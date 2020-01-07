// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "alicdn_decode_coordinator.h"

namespace flutter {

AliCDNDecodeCoordinator::AliCDNDecodeCoordinator(
  std::shared_ptr<fml::ConcurrentTaskRunner> concurrentTaskRunner)
: concurrentTaskRunner_(std::move(concurrentTaskRunner)) {
  
}

void AliCDNDecodeCoordinator::postTask(uint64_t memoryUsing, const fml::closure& task) {
  if (allowToRun(memoryUsing)) {
    runTask(memoryUsing, task);
  }
  else {
    taskMutex_.lock();
    taskList_.emplace_back(memoryUsing, task);
    taskMutex_.unlock();
  }
}

void AliCDNDecodeCoordinator::finishTask(uint64_t memoryUsing) {
  taskRunning_.fetch_sub(1);
  memoryUsed_.fetch_sub(memoryUsing);
  checkTasks();
}

void AliCDNDecodeCoordinator::updateCapacity(uint32_t maxConcurrentCount, uint64_t maxMemoryUsing) {
  if (maxConcurrentCount >= 2) {
    maxConcurrentCount_ = maxConcurrentCount;
  }
  
  if (maxMemoryUsing_ >= 10 * MegaBytes) {
    maxMemoryUsing_ = maxMemoryUsing;
  }
}

bool AliCDNDecodeCoordinator::allowToRun(uint64_t memoryUsing) {
  uint64_t memoryUsed = memoryUsed_.load();
  uint32_t taskRunning = taskRunning_.load();
  return (memoryUsing + memoryUsed <= maxMemoryUsing_ && taskRunning < maxConcurrentCount_) || (taskRunning == 0);
}

void AliCDNDecodeCoordinator::runTask(uint64_t memoryUsing, const fml::closure& task) {
  taskRunning_.fetch_add(1);
  memoryUsed_.fetch_add(memoryUsing);
  concurrentTaskRunner_->PostTask(task);
}

void AliCDNDecodeCoordinator::checkTasks() {
  std::lock_guard<std::mutex> lock(taskMutex_);
  for (auto it = taskList_.begin(); it != taskList_.end(); ) {
    if (allowToRun(it->first)) {
      runTask(it->first, it->second);
      it = taskList_.erase(it);
    }
    else {
      ++ it;
    }
  }
}

}  // namespace flutter
