/*--------------------------------------------------------------------------
Copyright (c) 2018, The Linux Foundation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name of The Linux Foundation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
--------------------------------------------------------------------------*/

#include <stdio.h>
#include <sys/time.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <unistd.h>

#include "SidebandStreamHandle.h"

using namespace android;

/*****************************************************************************/

int bufferWidth = 1280;
int bufferHeight = 768;
int color_format = 0;
int compressed_usage = 0;
SidebandHandleBase *mSidebandHandleBaseProducer;
SidebandHandleBase *mSidebandHandleBaseConsumer;

int testProducer(SidebandStreamHandle *mSidebandStreamHandle){
    mSidebandHandleBaseProducer = mSidebandStreamHandle->mHandleProducer(bufferWidth, bufferHeight, color_format, compressed_usage);
    printf("testProducer: Get bufferWidth=%d, bufferHeight=%d, color_format=%d, compressed_usage=%d, sidebandhandle_id=%d\n",
        mSidebandHandleBaseProducer->getBufferWidth(), mSidebandHandleBaseProducer->getBufferHeight(), mSidebandHandleBaseProducer->getColorFormat(),
        mSidebandHandleBaseProducer->getCompressedUsage(), mSidebandHandleBaseProducer->getSidebandHandleId());

    if((mSidebandHandleBaseProducer->getBufferWidth()==bufferWidth)&&(mSidebandHandleBaseProducer->getBufferHeight()==bufferHeight)&&
        (mSidebandHandleBaseProducer->getColorFormat()==color_format)&&(mSidebandHandleBaseProducer->getCompressedUsage()==compressed_usage)){
        return 0;
    }else{
        return -1;
    }
}

int testConsumer(SidebandStreamHandle *mSidebandStreamHandle){
//on real case the mSidebandHandleBaseProducer will be transfered by setsidebandstream() on framework.
//1. validate received sideband handle first
    if(SidebandHandleBase::validate((native_handle *)mSidebandHandleBaseProducer)!=0){
        printf("testConsumer: invalidate mSidebandHandleBaseProducer sideband handle\n");
        return -1;
    }
//2. create your own sideband handle by received sideband handle
    SidebandHandleBase *mSidebandHandleBaseConsumer = mSidebandStreamHandle->mHandleConsumer((native_handle *)mSidebandHandleBaseProducer);

//3. validate your own sideband handle
    if(SidebandHandleBase::validate((native_handle *)mSidebandHandleBaseConsumer)!=0){
        printf("testConsumer: invalidate mSidebandHandleBaseConsumer sideband handle\n");
        return -1;
    }

//4. get all the values from the sideband handle
    printf("testConsumer: Get bufferWidth=%d, bufferHeight=%d, color_format=%d, compressed_usage=%d, sidebandhandle_id=%d\n",
        mSidebandHandleBaseConsumer->getBufferWidth(), mSidebandHandleBaseConsumer->getBufferHeight(), mSidebandHandleBaseConsumer->getColorFormat(),
        mSidebandHandleBaseConsumer->getCompressedUsage(), mSidebandHandleBaseConsumer->getSidebandHandleId());

    if((mSidebandHandleBaseConsumer->getBufferWidth()==bufferWidth)&&(mSidebandHandleBaseConsumer->getBufferHeight()==bufferHeight)&&
        (mSidebandHandleBaseConsumer->getColorFormat()==color_format)&&(mSidebandHandleBaseConsumer->getCompressedUsage()==compressed_usage)){
        return 0;
    }else{
        return -1;
    }
}


int main ()
{
    int ret = 0;

    //used for open the libsideband.so and link the relative api
    SidebandStreamHandle *mSidebandStreamHandle = new SidebandStreamHandle();
    mSidebandStreamHandle->init();

    //For mm-avinput reference
    ret = testProducer(mSidebandStreamHandle);
    if(ret == 0){
        printf("testProducer successful!\n");
    }else{
        printf("testProducer failed!\n");
    }

    //For display reference.
    ret = testConsumer(mSidebandStreamHandle);
    if(ret == 0){
        printf("testProducer successful!\n");
    }else{
        printf("testProducer failed!\n");
    }

    delete mSidebandHandleBaseConsumer;
    delete mSidebandHandleBaseProducer;

    mSidebandStreamHandle->destroy();
    delete mSidebandStreamHandle;

    printf("test finish!\n");

    return 0;
}
