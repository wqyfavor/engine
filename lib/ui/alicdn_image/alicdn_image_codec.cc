// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "alicdn_image_codec.h"
#include "alicdn_decode_coordinator.h"
#include "third_party/tonic/logging/dart_invoke.h"
#include "third_party/tonic/dart_library_natives.h"
#include "third_party/tonic/dart_binding_macros.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkImage.h"
#include "third_party/skia/include/core/SkImageInfo.h"
#include "third_party/skia/include/core/SkRefCnt.h"
#include "flutter/fml/make_copyable.h"

namespace flutter {

static SkColorType ConvertColorType(AliCDNImageAdapter::ColorType source) {
  switch (source) {
    case AliCDNImageAdapter::ColorType::RGBA8888: return kRGBA_8888_SkColorType;
    case AliCDNImageAdapter::ColorType::BGRA8888: return kBGRA_8888_SkColorType;
    case AliCDNImageAdapter::ColorType::RGB565: return kRGB_565_SkColorType;
    case AliCDNImageAdapter::ColorType::ARGB4444: return kARGB_4444_SkColorType;
    case AliCDNImageAdapter::ColorType::Alpha8: return kAlpha_8_SkColorType;
    default: return kUnknown_SkColorType;
  }
}

static SkAlphaType ConvertAlphaType(AliCDNImageAdapter::AlphaType source) {
  switch (source) {
    case AliCDNImageAdapter::AlphaType::Opaque: return kOpaque_SkAlphaType;
    case AliCDNImageAdapter::AlphaType::Premul: return kPremul_SkAlphaType;
    case AliCDNImageAdapter::AlphaType::Unpremul: return kUnpremul_SkAlphaType;
    default: return kUnknown_SkAlphaType;
  }
}

class AliCDNImageAdapter;
class AliCDNImageManager;

static AliCDNImageAdapter* alicdn_image_adapter;
static AliCDNImageManager* alicdn_image_manager;

class AliCDNImageManager {
public:
  AliCDNImageManager(const TaskRunners& runners,
                     std::shared_ptr<fml::ConcurrentTaskRunner> concurrentRunner,
                     fml::WeakPtr<IOManager> io_manager)
  : concurrentCoordinator_(concurrentRunner),
  runners_(std::move(runners)), ioManager_(std::move(io_manager)) {}
  
  const TaskRunners& runners() const { return runners_; }
  fml::WeakPtr<IOManager> ioManager() const { return ioManager_; }
  
  AliCDNImageAdapter::RequestId nextRequestId() { return requestId ++; }
  
  AliCDNDecodeCoordinator& concurrentCoordinator() { return concurrentCoordinator_; }
  void evaluateDeviceStatus() {
    bool shouldEvaluate = !initialDeviceStatusEvaluated_ ||
      alicdn_image_adapter->shouldEvaluateDeviceStatus();
    
    if (shouldEvaluate) {
      uint32_t cpu;
      uint64_t memory;
      alicdn_image_adapter->evaluateDeviceStatus(cpu, memory);
      concurrentCoordinator_.updateCapacity(cpu, memory);
      initialDeviceStatusEvaluated_ = true;
    }
  }
  
private:
  AliCDNImageAdapter::RequestId requestId = 0;
  
  bool initialDeviceStatusEvaluated_ = false;
  AliCDNDecodeCoordinator concurrentCoordinator_;
  
  TaskRunners runners_;
  std::shared_ptr<fml::ConcurrentTaskRunner> concurrentTaskRunner_;
  fml::WeakPtr<IOManager> ioManager_;
};

void SetAliCDNImageAdapter(AliCDNImageAdapter* adapter) {
  if (alicdn_image_adapter == nullptr) {
    alicdn_image_adapter = adapter;
  }
}

void InitializeAliCDNImageManager(const TaskRunners& runners,
                                  std::shared_ptr<fml::ConcurrentTaskRunner> concurrent_task_runner,
                                  fml::WeakPtr<IOManager> io_manager) {
  if (alicdn_image_manager == nullptr) {
    alicdn_image_manager = new AliCDNImageManager(runners, concurrent_task_runner, io_manager);
  }
}

static SkiaGPUObject<SkImage> UploadTexture(AliCDNImageAdapter::Bitmap bitmap) {
  SkImageInfo imageInfo = SkImageInfo::Make(bitmap.width,
                                            bitmap.height,
                                            ConvertColorType(bitmap.colorType),
                                            ConvertAlphaType(bitmap.alphaType));
  SkPixmap pixmap(imageInfo, bitmap.pixels, bitmap.bytesPerRow);
  
  SkiaGPUObject<SkImage> result;
  alicdn_image_manager->ioManager()->GetIsGpuDisabledSyncSwitch()->Execute(
      fml::SyncSwitch::Handlers()
          .SetIfTrue([&result, &pixmap] {
            sk_sp<SkImage> texture = SkImage::MakeFromRaster(pixmap, nullptr, nullptr);
            result = {texture, nullptr};
          })
          .SetIfFalse([&result, context = alicdn_image_manager->ioManager()->GetResourceContext(),
                       &pixmap, queue = alicdn_image_manager->ioManager()->GetSkiaUnrefQueue()] {
            sk_sp<SkImage> texture = SkImage::MakeCrossContextFromPixmap(
                context.get(),  // context
                pixmap,         // pixmap
                true,           // buildMips,
                true            // limitToMaxTextureSize
            );
            if (!texture) {
              result = {};
            }
            else {
              result = {texture, queue};
            }
          }));
  return result;
}

AliCDNImageFrameCodec::AliCDNImageFrameCodec(std::unique_ptr<AliCDNImageAdapter::RequestInfo> descriptor)
: descriptor_(std::move(descriptor)) {
  
}

AliCDNImageFrameCodec::~AliCDNImageFrameCodec() {
  cancel();
}

int AliCDNImageFrameCodec::frameCount() const {
  return platformImage_.frameCount;
}

int AliCDNImageFrameCodec::repetitionCount() const {
  return platformImage_.repetitionCount;
}

Dart_Handle AliCDNImageFrameCodec::getNextFrame(Dart_Handle callback) {
  if (!Dart_IsClosure(callback)) {
    return tonic::ToDart("Callback must be a function");
  }
  
  if (status_ == Status::Complete) {
    if (platformImage_.frameCount > 1) {
      // Multiframe images. We do not cache frame, always do decoding progressively.
      if (platformImage_.handle == 0) {
        // Fail to get platform GIF image instance.
        tonic::DartInvoke(callback, {Dart_Null()});
      }
      else if (callback) {
        getNextMultiframe(callback);
      }
      return Dart_Null();
    }
    else if (cachedFrame_) {
      tonic::DartInvoke(callback, {tonic::ToDart(cachedFrame_)});
      return Dart_Null();
    }
    else {
      // This should never happen. But if did happen, we change status to New and restart downloading image.
      status_ = Status::New;
    }
  }
  
  auto dartState = UIDartState::Current();

  // Keep the callback as persistent value and record VM state.
  callbacks_.emplace_back(dartState, callback);

  if (status_ == Status::Downloading) {
    // We are downloading images from platform.
    return Dart_Null();
  }
  
  status_ = Status::Downloading;
  requestId_ = alicdn_image_manager->nextRequestId();
  
  // Make sure that self is only deallocated on UI thread.
  fml::RefPtr<AliCDNImageFrameCodec>* codecRef = new fml::RefPtr<AliCDNImageFrameCodec>(this);
  
  alicdn_image_adapter->request(requestId_, *descriptor_,
    [codecRef] (AliCDNImageAdapter::PlatformImage image, AliCDNImageAdapter::ReleaseImageCallback&& release) {
    std::unique_ptr<fml::RefPtr<AliCDNImageFrameCodec>> innerCodecRef(codecRef);
    fml::RefPtr<AliCDNImageFrameCodec> codec(std::move(*innerCodecRef));
    
    if (image.handle == 0) {
      if (codec->isCanceled()) {
        return;
      }
      
      // Fail to get image from platform.
      fml::RefPtr<AliCDNImageFrameCodec>* codecRef2 = new fml::RefPtr<AliCDNImageFrameCodec>(codec.get());
      alicdn_image_manager->runners().GetUITaskRunner()->PostTask([codecRef2] {
        // Keep ref of codec instance until UI task finishes.
        std::unique_ptr<fml::RefPtr<AliCDNImageFrameCodec>> innerCodecRef(codecRef2);
        fml::RefPtr<AliCDNImageFrameCodec> codec(std::move(*innerCodecRef));
        codec->status_ = Status::Complete; // Complete but failed.
        auto state = codec->callbacks_.front().dart_state().lock();
        if (!state) {
          return;
        }
        tonic::DartState::Scope scope(state.get());
        Dart_Handle nullFrame = Dart_Null();
        for (const DartPersistentValue& callback : codec->callbacks_) {
          tonic::DartInvoke(callback.value(), {nullFrame});
        }
        codec->callbacks_.clear();
      });
    }
    else {
      bool platformImageAssigned = false;
      {
        // Keep platform image info.
        std::lock_guard<std::recursive_mutex> lock(codec->platformImageLock_);
        if (!codec->isCanceled()) {
          codec->platformImage_ = image;
          codec->releasePlatformImageCallback_ = release; // For GIF image, we must keep the platform image instance.
          platformImageAssigned = true;
        }
        else if (release) {
          release(image.handle);
        }
      }
      
      if (!platformImageAssigned) {
        return;
      }
      
      if (image.frameCount > 1) {
        // For GIF we trigger decoding the first frame on Dart thread.
        fml::RefPtr<AliCDNImageFrameCodec>* codecRef2 = new fml::RefPtr<AliCDNImageFrameCodec>(codec.get());
        alicdn_image_manager->runners().GetUITaskRunner()->PostTask([codecRef2] {
          (*codecRef2)->getNextMultiframe(nullptr);
          delete codecRef2;
        });
        return;
      }
      
      // Decode single image asynchronously on worker thread.
      uint64_t imageDecodingCost = image.width * image.height * 4; // approximate memory used for decoding
      fml::RefPtr<AliCDNImageFrameCodec>* codecRef2 = new fml::RefPtr<AliCDNImageFrameCodec>(codec.get());
      alicdn_image_manager->concurrentCoordinator().postTask(imageDecodingCost,
                                                             [imageDecodingCost, codecRef2] () {
        // Keep ref of codec until end.
        std::unique_ptr<fml::RefPtr<AliCDNImageFrameCodec>> innerCodecRef(codecRef2);
        fml::RefPtr<AliCDNImageFrameCodec> codec(std::move(*innerCodecRef));
        
        AliCDNImageAdapter::DecodeResult decodeResult;
        
        // Check platform image because it might be released by cancelling.
        {
          std::lock_guard<std::recursive_mutex> lock(codec->platformImageLock_);
          if (codec->isCanceled()) {
            // Tell concurrent runner that previous task finished.
            alicdn_image_manager->concurrentCoordinator().finishTask(imageDecodingCost);
            return; // Just return, do not do decoding.
          }
          
          if (codec->platformImage_.handle != 0) {
            decodeResult = alicdn_image_adapter->decode(codec->platformImage_); // Synchronous decode in lock.
          }
          else {
            // Tell concurrent runner that previous task finished.
            alicdn_image_manager->concurrentCoordinator().finishTask(imageDecodingCost);
            
            // Decoding failed, release platform image such as UIImage.
            codec->releasePlatformImage();
            
            return; // Just return, no need to handle callbacks.
          }
        }
        
        AliCDNImageAdapter::Bitmap& bitmap = decodeResult.first;
        AliCDNImageAdapter::ReleaseBitmapCallback& releaseBitmap = decodeResult.second;
        
        if (bitmap.pixels == nullptr) {
          // Tell concurrent runner that previous task finished.
          alicdn_image_manager->concurrentCoordinator().finishTask(imageDecodingCost);
          
          // Decoding failed, release platform image such as UIImage.
          codec->releasePlatformImage();
          
          fml::RefPtr<AliCDNImageFrameCodec>* codecRef3 = new fml::RefPtr<AliCDNImageFrameCodec>(codec.get());
          alicdn_image_manager->runners().GetUITaskRunner()->PostTask([codecRef3] {
            // Keep ref of codec instance until UI task finishes.
            std::unique_ptr<fml::RefPtr<AliCDNImageFrameCodec>> innerCodecRef(codecRef3);
            fml::RefPtr<AliCDNImageFrameCodec> codec(std::move(*innerCodecRef));
            codec->status_ = Status::Complete; // Complete but failed.
            auto state = codec->callbacks_.front().dart_state().lock();
            if (!state) {
              return;
            }
            tonic::DartState::Scope scope(state.get());
            Dart_Handle nullFrame = Dart_Null();
            for (const DartPersistentValue& callback : codec->callbacks_) {
              tonic::DartInvoke(callback.value(), {nullFrame});
            }
            codec->callbacks_.clear();
          });
        }
        else {
          // Upload the bitmap to GPU on IO thread.
          fml::RefPtr<AliCDNImageFrameCodec>* codecRef3 = new fml::RefPtr<AliCDNImageFrameCodec>(codec.get());
          alicdn_image_manager->runners().GetIOTaskRunner()->PostTask([imageDecodingCost, bitmap, releaseBitmap, codecRef3] () {
            // Keep ref of codec instance until UI task finishes.
            std::unique_ptr<fml::RefPtr<AliCDNImageFrameCodec>> innerCodecRef(codecRef3);
            fml::RefPtr<AliCDNImageFrameCodec> codec(std::move(*innerCodecRef));
            
            // Check again before we really do uploading to GPU.
            if (codec->isCanceled()) {
              // Release pixels, because pixels might be copied besides platform image instance.
              if (releaseBitmap) {
                releaseBitmap(bitmap);
              }
              
              // Tell concurrent runner that previous task finished.
              alicdn_image_manager->concurrentCoordinator().finishTask(imageDecodingCost);
              
              return;
            }
            
            auto ioManager = alicdn_image_manager->ioManager();
            bool ioStatusValid = ioManager && ioManager->GetResourceContext() && ioManager->GetSkiaUnrefQueue();
            
            SkiaGPUObject<SkImage> uploaded;
            if (ioStatusValid) {
              uploaded = UploadTexture(bitmap); // This line do upload texture to GPU memory.
            }
            
            // Release pixels, because pixels might be copied besides platform image instance.
            if (releaseBitmap) {
              releaseBitmap(bitmap);
            }
            
            // Tell concurrent runner that previous task finished.
            alicdn_image_manager->concurrentCoordinator().finishTask(imageDecodingCost);
            
            // All done, release platform image instance such as UIImage.
            codec->releasePlatformImage();
            
            // Go back to UI thread and notify dart widgets.
            fml::RefPtr<AliCDNImageFrameCodec>* codecRef4 = new fml::RefPtr<AliCDNImageFrameCodec>(codec.get());
            alicdn_image_manager->runners().GetUITaskRunner()->PostTask(fml::MakeCopyable([codecRef4,
                                                                         textureImage = std::move(uploaded)] () mutable {
              // Keep ref of codec instance until UI task finishes.
              std::unique_ptr<fml::RefPtr<AliCDNImageFrameCodec>> innerCodecRef(codecRef4);
              fml::RefPtr<AliCDNImageFrameCodec> codec(std::move(*innerCodecRef));
              if (codec->isCanceled()) {
                return;
              }
              
              codec->status_ = Status::Complete; // Complete
              auto state = codec->callbacks_.front().dart_state().lock();
              if (!state) {
                return;
              }
              tonic::DartState::Scope scope(state.get());
              
              // Convert to cached frame.
              if (textureImage.get()) {
                auto canvasImage = fml::MakeRefCounted<CanvasImage>();
                canvasImage->set_image(std::move(textureImage));
                codec->cachedFrame_ = fml::MakeRefCounted<FrameInfo>(std::move(canvasImage), 0 /* duration */);
              }
              
              Dart_Handle frame = tonic::ToDart(codec->cachedFrame_);
              for (const DartPersistentValue& callback : codec->callbacks_) {
                tonic::DartInvoke(callback.value(), {frame});
              }
              codec->callbacks_.clear();
            }));
          });
        }
      });
    }
  });

  return Dart_Null();
}

size_t AliCDNImageFrameCodec::GetAllocationSize() {
  return sizeof(AliCDNImageFrameCodec);
}

bool AliCDNImageFrameCodec::isCanceled()
{
  return canceled_.load();
}

void AliCDNImageFrameCodec::cancel() {
  canceled_.store(true);
  
  if (status_ == Status::Downloading) {
    alicdn_image_adapter->cancel(requestId_);
  }
  
  releasePlatformImage(); // For GIF, we also need to release platform image instance.
  cachedFrame_ = nullptr; // Release the texture.
  status_ = Status::Complete;
  callbacks_.clear();
}

void AliCDNImageFrameCodec::releasePlatformImage() {
  std::lock_guard<std::recursive_mutex> lock(platformImageLock_);
  if (platformImage_.handle != 0 && releasePlatformImageCallback_) {
    releasePlatformImageCallback_(platformImage_.handle);
  }
  platformImage_.handle = 0;
  releasePlatformImageCallback_ = nullptr;
}

void AliCDNImageFrameCodec::getNextMultiframe(Dart_Handle callback) {
  if (isCanceled()) {
    return;
  }
  
  std::shared_ptr<DartPersistentValue> uniqueCallbackState;
  if (callback) {
    // Have callback means decode for only one callback listener.
    FML_DCHECK(alicdn_image_manager->runners().GetUITaskRunner()->RunsTasksOnCurrentThread());
    uniqueCallbackState = std::make_shared<DartPersistentValue>(tonic::DartState::Current(), callback);
  }
  else if (callbacks_.empty()) {
    return;
  }
   
  // Make sure that self is only deallocated on UI thread.
  fml::RefPtr<AliCDNImageFrameCodec>* codecRef = new fml::RefPtr<AliCDNImageFrameCodec>(this);
  uint64_t imageDecodingCost = platformImage_.width * platformImage_.height * 4; // approximate memory used for decoding
  alicdn_image_manager->concurrentCoordinator().postTask(imageDecodingCost,
                                                         [codecRef,
                                                          imageDecodingCost,
                                                          frameIndex = nextFrameIndex_,
                                                          uniqueCallbackState] () {
    // Keep ref of codec instance until UI task finishes.
    std::unique_ptr<fml::RefPtr<AliCDNImageFrameCodec>> innerCodecRef(codecRef);
    fml::RefPtr<AliCDNImageFrameCodec> codec(std::move(*innerCodecRef));
    
    AliCDNImageAdapter::DecodeResult decodeResult;
    
    // Check platform image because it might be released by cancelling.
    {
      std::lock_guard<std::recursive_mutex> lock(codec->platformImageLock_);
      if (codec->isCanceled()) {
        // Tell concurrent runner that previous task finished.
        alicdn_image_manager->concurrentCoordinator().finishTask(imageDecodingCost);
        return; // Just return, do not do decoding.
      }
      
      if (codec->platformImage_.handle != 0) {
        decodeResult = alicdn_image_adapter->decode(codec->platformImage_, frameIndex); // Synchronous decode in lock.
      }
      else {
        // Tell concurrent runner that previous task finished.
        alicdn_image_manager->concurrentCoordinator().finishTask(imageDecodingCost);
        
        // Decoding failed, release platform image such as UIImage.
        codec->releasePlatformImage();
        
        return; // Just return, no need to handle callbacks.
      }
    }
    
    AliCDNImageAdapter::Bitmap& bitmap = decodeResult.first;
    AliCDNImageAdapter::ReleaseBitmapCallback& releaseBitmap = decodeResult.second;
    
    if (bitmap.pixels == nullptr) {
      // Tell concurrent runner that previous task finished.
      alicdn_image_manager->concurrentCoordinator().finishTask(imageDecodingCost);
      
      // Any frame decoding failed, release platform image such as UIImage.
      codec->releasePlatformImage();
      
      fml::RefPtr<AliCDNImageFrameCodec>* codecRef2 = new fml::RefPtr<AliCDNImageFrameCodec>(codec.get());
      alicdn_image_manager->runners().GetUITaskRunner()->PostTask([codecRef2, uniqueCallbackState] {
        // Keep ref of codec instance until UI task finishes.
        std::unique_ptr<fml::RefPtr<AliCDNImageFrameCodec>> innerCodecRef(codecRef2);
        fml::RefPtr<AliCDNImageFrameCodec> codec(std::move(*innerCodecRef));
        codec->status_ = Status::Complete; // Complete but failed.
        auto state = uniqueCallbackState ? uniqueCallbackState->dart_state().lock() :
          codec->callbacks_.front().dart_state().lock();
        if (!state) {
          return;
        }
        tonic::DartState::Scope scope(state.get());
        Dart_Handle nullFrame = Dart_Null();
        if (uniqueCallbackState) {
          tonic::DartInvoke(uniqueCallbackState->value(), {nullFrame});
        }
        else {
          for (const DartPersistentValue& callback : codec->callbacks_) {
            tonic::DartInvoke(callback.value(), {nullFrame});
          }
          codec->callbacks_.clear();
        }
      });
    }
    else {
      // Upload the bitmap to GPU on IO thread.
      fml::RefPtr<AliCDNImageFrameCodec>* codecRef2 = new fml::RefPtr<AliCDNImageFrameCodec>(codec.get());
      alicdn_image_manager->runners().GetIOTaskRunner()->PostTask([imageDecodingCost,
                                                                   bitmap, releaseBitmap, codecRef2,
                                                                   uniqueCallbackState] () {
        // Keep ref of codec instance until UI task finishes.
        std::unique_ptr<fml::RefPtr<AliCDNImageFrameCodec>> innerCodecRef(codecRef2);
        fml::RefPtr<AliCDNImageFrameCodec> codec(std::move(*innerCodecRef));
        
        // Check again before we really do uploading to GPU.
        if (codec->isCanceled()) {
          // Release pixels, because pixels might be copied besides platform image instance.
          if (releaseBitmap) {
            releaseBitmap(bitmap);
          }
          
          // Tell concurrent runner that previous task finished.
          alicdn_image_manager->concurrentCoordinator().finishTask(imageDecodingCost);
          
          return;
        }
        
        auto ioManager = alicdn_image_manager->ioManager();
        bool ioStatusValid = ioManager && ioManager->GetResourceContext() && ioManager->GetSkiaUnrefQueue();
        
        SkiaGPUObject<SkImage> uploaded;
        if (ioStatusValid) {
          uploaded = UploadTexture(bitmap);
        }
        
        // Release pixels, because pixels might be copied besides platform image instance.
        if (releaseBitmap) {
          releaseBitmap(bitmap);
        }
        
        // Tell concurrent runner that previous task finished.
        alicdn_image_manager->concurrentCoordinator().finishTask(imageDecodingCost);
        
        // Go back to UI thread and notify dart widgets.
        fml::RefPtr<AliCDNImageFrameCodec>* codecRef3 = new fml::RefPtr<AliCDNImageFrameCodec>(codec.get());
        alicdn_image_manager->runners().GetUITaskRunner()->PostTask(fml::MakeCopyable([codecRef3,
                                                                                       textureImage = std::move(uploaded),
                                                                                       uniqueCallbackState] () mutable {
          // Keep ref of codec instance until UI task finishes.
          std::unique_ptr<fml::RefPtr<AliCDNImageFrameCodec>> innerCodecRef(codecRef3);
          fml::RefPtr<AliCDNImageFrameCodec> codec(std::move(*innerCodecRef));
          if (codec->isCanceled()) {
            return;
          }
          
          codec->status_ = Status::Complete; // Complete
          auto state = uniqueCallbackState ? uniqueCallbackState->dart_state().lock() :
            codec->callbacks_.front().dart_state().lock();
          if (!state) {
            return;
          }
          tonic::DartState::Scope scope(state.get());
          
          // Convert to frame with duration info.
          fml::RefPtr<FrameInfo> frameInfo = NULL;
          if (textureImage.get()) {
            auto canvasImage = fml::MakeRefCounted<CanvasImage>();
            canvasImage->set_image(std::move(textureImage));
            frameInfo = fml::MakeRefCounted<FrameInfo>(std::move(canvasImage),
                                                       codec->platformImage_.durationInMs /
                                                       codec->platformImage_.frameCount);
          }
          
          Dart_Handle frame = tonic::ToDart(frameInfo);
          if (uniqueCallbackState) {
            tonic::DartInvoke(uniqueCallbackState->value(), {frame});
          }
          else {
            for (const DartPersistentValue& callback : codec->callbacks_) {
              tonic::DartInvoke(callback.value(), {frame});
            }
            codec->callbacks_.clear();
          }
        }));
      });
    }
  });
  
  nextFrameIndex_ = (nextFrameIndex_ + 1) % platformImage_.frameCount;
}

/*
 Arguments:
    String url,
    _Callback<Codec> callback,
    int targetWidth,
    int targetHeight,
    String tag,
    bool sharp,
    int quality,
    int webp,
    int heic,
    int multiframe,
    Map<String, String> extraInfo
 */
static void AliCDNInstantiateImageCodec(Dart_NativeArguments args) {
  if (alicdn_image_adapter == nullptr ||
      alicdn_image_manager == nullptr) {
    Dart_SetReturnValue(args, tonic::ToDart("Ali CDN image decoder not initialized."));
    return;
  }
  
  // Reevaluate device status for balanced memory usage.
  alicdn_image_manager->evaluateDeviceStatus();
  
  std::unique_ptr<AliCDNImageAdapter::RequestInfo> descriptor =
    std::make_unique<AliCDNImageAdapter::RequestInfo>();
  
  Dart_Handle exception = nullptr;
  descriptor->url = tonic::DartConverter<std::string>::FromArguments(args, 0, exception);
  
  Dart_Handle callback = Dart_GetNativeArgument(args, 1);
  if (!Dart_IsClosure(callback)) {
    Dart_SetReturnValue(args, tonic::ToDart("Callback must be a function."));
    return;
  }
  
  descriptor->targetWidth = tonic::DartConverter<int>::FromArguments(args, 2, exception);
  descriptor->targetHeight = tonic::DartConverter<int>::FromArguments(args, 3, exception);
  descriptor->tag = tonic::DartConverter<std::string>::FromArguments(args, 4, exception);
  descriptor->policy.sharp = tonic::DartConverter<bool>::FromArguments(args, 5, exception);
  descriptor->policy.quality =
    static_cast<AliCDNImageQuality>(tonic::DartConverter<int>::FromArguments(args, 6, exception));
  descriptor->policy.webp =
    static_cast<AliCDNImageWebP>(tonic::DartConverter<int>::FromArguments(args, 7, exception));
  descriptor->policy.heic =
    static_cast<AliCDNImageHEIC>(tonic::DartConverter<int>::FromArguments(args, 8, exception));
  descriptor->policy.multiframe =
    static_cast<AliCDNImageMultiFrame>(tonic::DartConverter<int>::FromArguments(args, 9, exception));
  Dart_Handle extraInfoVM = Dart_GetNativeArgument(args, 10);
  if (exception) {
    Dart_SetReturnValue(args, tonic::ToDart("Failed to get url."));
    return;
  }
  
  // Check extraInfo
  if (!Dart_IsNull(extraInfoVM)) {
    bool isValidExtraInfo = false;
    do {
      if (!Dart_IsMap(extraInfoVM)) {
        break;
      }
      
      Dart_Handle keys = Dart_MapKeys(extraInfoVM);
      if (!Dart_IsList(keys)) {
        break;
      }
      
      intptr_t length = 0;
      Dart_ListLength(extraInfoVM, &length);
      if (length > 0) {
        bool isValidMap = true;
        for (intptr_t i = 0; i < length; i ++) {
          Dart_Handle k = Dart_ListGetAt(keys, i);
          if (!Dart_IsString(k)) {
            isValidMap = false;
            break;
          }
          
          Dart_Handle v = Dart_MapGetAt(extraInfoVM, k);
          if (!Dart_IsString(v)) {
            isValidMap = false;
            break;
          }
          
          const char* ks = nullptr;
          const char* vs = nullptr;
          Dart_StringToCString(k, &ks);
          Dart_StringToCString(v, &vs);
          if (ks == nullptr || vs == nullptr) {
            isValidMap = false;
            break;
          }
          descriptor->extraInfo.insert({ks, vs});
        }
        
        if (!isValidMap) {
          break;
        }
      }
      
      isValidExtraInfo = true;
    } while (false);
    
    if (!isValidExtraInfo) {
      Dart_SetReturnValue(args, tonic::ToDart("Invalid extraInfo."));
      return;
    }
  }
  
  // Return cdn codec.
  fml::RefPtr<Codec> ui_codec = fml::MakeRefCounted<AliCDNImageFrameCodec>(std::move(descriptor));
  tonic::DartInvoke(callback, {ToDart(ui_codec)});
}

IMPLEMENT_WRAPPERTYPEINFO(ui, AliCDNImageFrameCodec);

#define FOR_EACH_BINDING(V) \
  V(AliCDNImageFrameCodec, cancel)

FOR_EACH_BINDING(DART_NATIVE_CALLBACK)

void AliCDNImageFrameCodec::RegisterNatives(tonic::DartLibraryNatives* natives) {
  natives->Register({
    {"AliCDNInstantiateImageCodec", AliCDNInstantiateImageCodec, 11, true},
  });
  natives->Register({FOR_EACH_BINDING(DART_REGISTER_NATIVE)});
}

}  // namespace flutter
