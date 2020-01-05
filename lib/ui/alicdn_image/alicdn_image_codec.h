// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "AliCDNImage.h"
#include "flutter/lib/ui/painting/codec.h"
#include "flutter/common/task_runners.h"
#include "flutter/fml/concurrent_message_loop.h"
#include "flutter/lib/ui/io_manager.h"

namespace flutter {

class AliCDNImageFrameCodec : public Codec {
  DEFINE_WRAPPERTYPEINFO();
    
public:
  AliCDNImageFrameCodec(std::unique_ptr<AliCDNImageAdapter::RequestInfo> descriptor);
  ~AliCDNImageFrameCodec();
  
  static void RegisterNatives(tonic::DartLibraryNatives* natives);

  // Codec
  int frameCount() const override;
  int repetitionCount() const override;
  Dart_Handle getNextFrame(Dart_Handle callback) override;

  // tonic::DartWrappable
  size_t GetAllocationSize() override;
  
  // Cancel image request from platform.
  void cancel();

private:
  std::unique_ptr<AliCDNImageAdapter::RequestInfo> descriptor_;
  
  enum class Status { New, Downloading, Complete };
  Status status_ = Status::New;
  
  fml::RefPtr<FrameInfo> cachedFrame_;
  std::vector<DartPersistentValue> callbacks_;
  
  // Platform image info.
  AliCDNImageAdapter::RequestId requestId_;
  AliCDNImageAdapter::PlatformImage platformImage_;
  
  // For multiframe images.
  int nextFrameIndex_ = 0;

  FML_FRIEND_MAKE_REF_COUNTED(AliCDNImageFrameCodec);
  FML_FRIEND_REF_COUNTED_THREAD_SAFE(AliCDNImageFrameCodec);
};

extern void InitializeAliCDNImageManager(const TaskRunners& runners,
                                         std::shared_ptr<fml::ConcurrentTaskRunner> concurrent_task_runner,
                                         fml::WeakPtr<IOManager> io_manager);

}  // namespace flutter
