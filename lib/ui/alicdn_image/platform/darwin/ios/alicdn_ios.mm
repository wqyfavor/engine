#import <Foundation/Foundation.h>
#include "flutter/lib/ui/alicdn_image/alicdn_image.h"

namespace flutter {
  class AliCDNImageIOSAdapter: public AliCDNImageAdapter {
  public:
    virtual void request(RequestId rid,
                         RequestInfo&& requestInfo,
                         RequestCallback&& callback) override {
        RequestCallback callbackCpy = callback;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(5 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            PlatformImage image;
            callbackCpy(image, [] (PlatformHandle handle) {
                
            });
        });
    }
    
    virtual void cancel(RequestId rid) override {
      
    }
    
    virtual void decode() override {
      
    }
  };
}

@interface AliCDNBridgeTest : NSObject
{
    
}
@end

@implementation AliCDNBridgeTest

+ (void)load
{
  flutter::SetAliCDNImageAdapter(new flutter::AliCDNImageIOSAdapter());
}

@end



