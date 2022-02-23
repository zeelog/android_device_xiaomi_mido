#include <gui/SurfaceComposerClient.h>

using namespace android;

extern "C" {

void _ZN7android7SurfaceC1ERKNS_2spINS_22IGraphicBufferProducerEEEbRKNS1_INS_7IBinderEEE(
    void* thisptr, const sp<IGraphicBufferProducer>& bufferProducer, bool controlledByApp, const sp<IBinder>& surfaceControlHandle);

void _ZN7android7SurfaceC1ERKNS_2spINS_22IGraphicBufferProducerEEEb(
    void* thisptr, const sp<IGraphicBufferProducer> &bufferProducer, bool controlledByApp) {
        _ZN7android7SurfaceC1ERKNS_2spINS_22IGraphicBufferProducerEEEbRKNS1_INS_7IBinderEEE(thisptr, bufferProducer, controlledByApp, nullptr);
}

}
