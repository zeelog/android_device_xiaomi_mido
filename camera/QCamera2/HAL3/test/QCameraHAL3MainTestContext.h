/* Copyright (c) 2016, The Linux Foundation. All rights reserved.
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
*     * Neither the name of The Linux Foundation nor the names of its
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
#ifndef __HAL3TEST_H__
#define __HAL3TEST_H__

#include <stdio.h>
#include <stdlib.h>
#include <iostream>
namespace qcamera {


typedef enum
{
    MENU_BASE = 0,
    MENU_START_PREVIEW,
    MENU_START_VIDEO,
    MENU_START_CAPTURE,
    MENU_START_RAW_CAPTURE,
    MENU_EXIT
} menu_id;

typedef struct {
    menu_id base_menu;
    char *  basemenu_name;
} HAL3TEST_BASE_MENU_TBL_T;

typedef struct {
    char * menu_name;
} HAL3TEST_SENSOR_MENU_TBL_T;

typedef struct {
    menu_id main_menu;
    char * menu_name;
} CAMERA_BASE_MENU_TBL_T;

class CameraHAL3Base;
class MainTestContext
{
    int choice;
    bool mTestRunning;
public:
    MainTestContext();
    int hal3appGetUserEvent();
    int hal3appDisplaySensorMenu(uint8_t );
    void hal3appDisplayCapabilityMenu();
    int hal3appDisplayPreviewMenu();
    int hal3appDisplayVideoMenu();
    void hal3appDisplayRawCaptureMenu();
    void hal3appDisplaySnapshotMenu();
    void hal3appDisplayExitMenu();
    int hal3appPrintMenu();
};

}

#endif