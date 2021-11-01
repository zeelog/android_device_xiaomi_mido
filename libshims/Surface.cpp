#include <gui/Surface.h>

namespace android {

extern "C" void
_ZN7android7SurfaceC1ERKNS_2spINS_22IGraphicBufferProducerEEEbRKNS1_INS_7IBinderEEE(
    const sp<IGraphicBufferProducer> &, bool, sp<IBinder> &);

extern "C" void _ZN7android7SurfaceC1ERKNS_2spINS_22IGraphicBufferProducerEEEb(
    const sp<IGraphicBufferProducer> &bufferProducer, bool controlledByApp) {
  sp<IBinder> handle = static_cast<sp<IBinder>>(nullptr);
  return _ZN7android7SurfaceC1ERKNS_2spINS_22IGraphicBufferProducerEEEbRKNS1_INS_7IBinderEEE(
      bufferProducer, controlledByApp, handle);
}

} // namespace android
