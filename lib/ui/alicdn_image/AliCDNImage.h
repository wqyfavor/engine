// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <map>
#include <functional>

#define ALICDN_IMAGE_EXPORT __attribute__((visibility("default")))

namespace flutter {

enum class AliCDNImageQuality {
  Default = 0,
  Original,
  Low, // q50
  Normal, // q75
  High, // q90
};

enum class AliCDNImageWebP {
  Default = 0,
  Never,
  Always,
};

enum class AliCDNImageHEIC {
  Default = 0,
  Never,
  Always,
};

// GIF/APNG
enum class AliCDNImageMultiFrame {
  Default = 0,
  Compress,
};

struct ALICDN_IMAGE_EXPORT AliCDNImagePolicy {
  bool sharp = false;
  AliCDNImageQuality quality = AliCDNImageQuality::Default;
  AliCDNImageWebP webp = AliCDNImageWebP::Default;
  AliCDNImageHEIC heic = AliCDNImageHEIC::Default;
  AliCDNImageMultiFrame multiframe = AliCDNImageMultiFrame::Default;
};

class ALICDN_IMAGE_EXPORT AliCDNImageAdapter {
public:
  struct RequestInfo {
    std::string url;
    std::string tag;
    int targetWidth = 0;
    int targetHeight = 0;
    AliCDNImagePolicy policy;
    std::map<std::string, std::string> extraInfo;
  };
  
  using PlatformHandle = uintptr_t;
  struct PlatformImage {
    PlatformHandle handle = 0;
    int frameCount = 1; // for multiframe image
    int repetitionCount = 0; // 0 means infinite.
    int durationInMs = 0; // in milliseconds
  };
  
  enum class AlphaType { Opaque, Premul, Unpremul };
  enum class ColorType { RGBA8888, BGRA8888, RGB565, ARGB4444, Alpha8 };
  
  struct Bitmap {
    void* pixels;
    int width;
    int height;
    AlphaType alphaType;
    ColorType colorType;
    size_t bytesPerRow;
    size_t bytesPerPixel;
    void* userData;
  };
  
  using RequestId = unsigned long;
  using ReleaseImageCallback = std::function<void(PlatformHandle handle)>;
  using RequestCallback = std::function<void(PlatformImage image, ReleaseImageCallback&& release)>;
  using ReleaseBitmapCallback = std::function<void(Bitmap bitmap)>;
  
  virtual void request(RequestId rid,
                       const RequestInfo& requestInfo,
                       RequestCallback&& callback) = 0;
  virtual void cancel(RequestId rid) = 0;
  
  virtual std::pair<Bitmap, ReleaseBitmapCallback> decode(PlatformImage image) = 0;
};

ALICDN_IMAGE_EXPORT extern void SetAliCDNImageAdapter(AliCDNImageAdapter* adapter);

}  // namespace flutter
