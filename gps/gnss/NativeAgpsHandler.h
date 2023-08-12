/* Copyright (c) 2020, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation, nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#ifndef NATIVEAGPSHANDLER_H
#define NATIVEAGPSHANDLER_H

#include <cinttypes>
#include <string.h>
#include <gps_extended_c.h>
#include <IDataItemObserver.h>
#include <IDataItemCore.h>
#include <IOsObserver.h>

using namespace std;
using loc_core::IOsObserver;
using loc_core::IDataItemObserver;
using loc_core::IDataItemCore;

class GnssAdapter;

class NativeAgpsHandler : public IDataItemObserver {
public:
    NativeAgpsHandler(IOsObserver* sysStatObs, GnssAdapter& adapter);
    ~NativeAgpsHandler();
    AgpsCbInfo getAgpsCbInfo();
    // IDataItemObserver overrides
    virtual void notify(const list<IDataItemCore*>& dlist);
    inline virtual void getName(string& name);
private:
    static NativeAgpsHandler* sLocalHandle;
    static void agnssStatusIpV4Cb(AGnssExtStatusIpV4 statusInfo);
    void processATLRequestRelease(AGnssExtStatusIpV4 statusInfo);
    IOsObserver* mSystemStatusObsrvr;
    bool mConnected;
    string mApn;
    GnssAdapter& mAdapter;
};

#endif // NATIVEAGPSHANDLER_H
