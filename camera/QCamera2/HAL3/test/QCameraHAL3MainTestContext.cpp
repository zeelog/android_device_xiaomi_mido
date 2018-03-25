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

#include "QCameraHAL3MainTestContext.h"
#include "QCameraHAL3Base.h"

namespace qcamera {

#define MAX_CAMERA_SUPPORTED 20

const CAMERA_BASE_MENU_TBL_T camera_main_menu_tbl[] = {
    {MENU_START_PREVIEW,             "To Start Preview"},
    {MENU_START_VIDEO,               "To Start Video"},
    {MENU_START_CAPTURE,             "To Capture"},
    {MENU_START_RAW_CAPTURE,         "To Raw Capture"},
    {MENU_EXIT,                      "EXIT"},
};

const HAL3TEST_SENSOR_MENU_TBL_T sensor_tbl[] = {
    {"Rear Camera"},
    {"Front Camera"},
};

CameraHAL3Base *mCamHal3Base = NULL;
pthread_mutex_t gCamLock = PTHREAD_MUTEX_INITIALIZER;

MainTestContext::MainTestContext()
{
    int i = 0;
    mTestRunning = false;
    mCamHal3Base = NULL;
}

int MainTestContext::hal3appGetUserEvent()
{
    char tc_buf[3];
    int choice;
    int rc = 0, req_capture = 0;
    int num_testcase = MENU_EXIT+1;
    int submenu_choice, num;
    int preview_restart;
    static int prev_menu_choice;
    uint8_t camid, num_of_cameras;
    if (mCamHal3Base != NULL) {
        delete mCamHal3Base;
    }
    mCamHal3Base = new CameraHAL3Base(0);
    num_of_cameras = mCamHal3Base->hal3appCameraTestLoad();
    if ((num_of_cameras <= 0) && (num_of_cameras >= MAX_CAMERA_SUPPORTED)) {
        LOGE("\n Supported Camera Value is wrong : %d", num_of_cameras);
        printf("\n Invalid Number Of Cameras");
        goto exit;
    }
    else {
        choice = hal3appDisplaySensorMenu(num_of_cameras);
        if (choice >= num_of_cameras || choice < 0) {
            printf("\n Unsupported Parameter");
            goto exit;
        }
        else {
            mCamHal3Base->mCameraIndex = choice;
            rc = mCamHal3Base->hal3appCameraLibOpen(choice);
        }
    }
    do {
        choice = hal3appPrintMenu();
        switch(choice) {
            case MENU_START_PREVIEW:
                mCamHal3Base->hal3appCameraPreviewInit(MENU_START_PREVIEW,
                        mCamHal3Base->mCameraIndex, PREVIEW_WIDTH, PREVIEW_HEIGHT);
                mCamHal3Base->mPreviewRunning = 1; mCamHal3Base->mVideoRunning = 0;
                mCamHal3Base->mSnapShotRunning = 0;
            break;

            case MENU_START_VIDEO:
                mCamHal3Base->hal3appCameraVideoInit(MENU_START_VIDEO,
                        mCamHal3Base->mCameraIndex, VIDEO_WIDTH, VIDEO_HEIGHT);
                mCamHal3Base->mPreviewRunning = 0; mCamHal3Base->mVideoRunning = 1;
                mCamHal3Base->mSnapShotRunning = 0;
            break;

            case MENU_START_CAPTURE:
                hal3appDisplaySnapshotMenu();
                req_capture = 3; preview_restart = 0;
                if (mCamHal3Base->mPreviewRunning == 1) {
                    preview_restart = 1;
                }
                mCamHal3Base->hal3appCameraCaptureInit(0, 0, req_capture);
                mCamHal3Base->mPreviewRunning = 0; mCamHal3Base->mVideoRunning = 0;
                mCamHal3Base->mSnapShotRunning = 1;
                if (preview_restart == 1) {
                    mCamHal3Base->hal3appCameraPreviewInit(MENU_START_PREVIEW,
                            mCamHal3Base->mCameraIndex, PREVIEW_WIDTH, PREVIEW_HEIGHT);
                    mCamHal3Base->mPreviewRunning = 1; mCamHal3Base->mVideoRunning = 0;
                    mCamHal3Base->mSnapShotRunning = 0;
                }
            break;

            case MENU_START_RAW_CAPTURE:
                hal3appDisplayRawCaptureMenu();
                req_capture = 3;
                mCamHal3Base->hal3appRawCaptureInit(0, 0, req_capture);
                mCamHal3Base->mPreviewRunning = 0; mCamHal3Base->mVideoRunning = 0;
                mCamHal3Base->mSnapShotRunning = 1;
            break;

            case MENU_EXIT:
                hal3appDisplayExitMenu();
            break;

            default:
                printf("\n Option not in Menu\n");
        }
    }while(choice != MENU_EXIT);
    exit:
    return 0;
}

int MainTestContext::hal3appDisplaySensorMenu(uint8_t num_of_cameras)
{
    int i, choice;
    printf("\n");
    printf("===========================================\n");
    printf("    Camera Sensor to be used:            \n");
    printf("===========================================\n\n");

    for ( i=0;i < num_of_cameras; i++) {
        if (i <= 1) {
            printf("\n Press %d to select %s", (i), sensor_tbl[i].menu_name);
        }
        else {
            printf("\n Press %d to select Camera%d", (i), i);
        }
    }
    printf("\n Enter your Choice:");
    fscanf(stdin, "%d", &choice);
    return choice;
}

void MainTestContext::hal3appDisplayCapabilityMenu()
{
    printf("\n");
    printf("===========================================\n");
    printf("      Sensor Capabilty are dumped at location:\n");
    printf("===========================================\n\n");
}

int MainTestContext::hal3appDisplayPreviewMenu()
{
    int choice;
    printf("\n");
    printf("===========================================\n");
    printf("Select Camera Preview Resolution:\n");
    printf("===========================================\n\n");
    printf("========Select Preview Resolutions================\n");
    printf("\nPress 1 .Aspect Ratio(4:3) Resolution 1440 X 1080");
    printf("\nPress 2 .Aspect Ratio(16:9) Resolution 1920 X 1080");
    printf("\n Enter your Choice:");
    fscanf(stdin, "%d", &choice);
    return choice;
}

int MainTestContext::hal3appDisplayVideoMenu()
{
    int choice1;
    printf("\n");
    printf("===========================================\n");
    printf("Testing Camera Recording on Different Resolution:\n");
    printf("===========================================\n\n");

    printf("========Select Video Resolutions================\n");
    printf("\nPress 1 .Aspect Ratio(4:3) Resolution 640 X 480");
    printf("\nPress 2 .Aspect Ratio(16:9) Resolution 1920 X 1080");
    printf("\nPress 3 .To select both");

    printf("\n Enter your Choice:");
    fscanf(stdin, "%d", &choice1);
    return choice1;
}

void MainTestContext::hal3appDisplayRawCaptureMenu()
{
    int req_cap;
    printf("\n");
    printf("===========================================\n");
    printf("Testing RAW Camera Capture on Different Resolution::\n");
    printf("===========================================\n\n");
}

void MainTestContext::hal3appDisplaySnapshotMenu()
{
    int req_cap;
    printf("\n");
    printf("===========================================\n");
    printf("Testing Normal Camera Capture on Resolution 5344 X 4008\n");
    printf("===========================================\n\n");
}


void MainTestContext::hal3appDisplayExitMenu()
{
    printf("\n");
    printf("===========================================\n");
    printf("      Exiting HAL3 APP test \n");
    printf("===========================================\n\n");
}

int MainTestContext::hal3appPrintMenu()
{
    int i, choice = 0;
    char ch = '0';
    printf("\n");
    printf("===========================================\n");
    printf("       HAL3 MENU \n");
    printf("===========================================\n\n");
    for ( i = 0; i < (int)(sizeof(camera_main_menu_tbl)/sizeof(camera_main_menu_tbl[0])); i++) {
        printf("\n Press %d to select %s", (i+1), camera_main_menu_tbl[i].menu_name);
    }
    printf("\n Enter your Choice:");
    do {
        std::cin >> ch;
    } while(!(ch >= '1' && ch <= '9'));
    choice = ch -'0';
    return choice;
}
}

int main()
{
    char tc_buf[3];
    int mode = 0;
    int rc = 0;
    qcamera::MainTestContext main_ctx;
    printf("Please Select Execution Mode:\n");
    printf("0: Menu Based 1: Regression\n");
    printf("\n Enter your choice:");
    fgets(tc_buf, 3, stdin);
    mode = tc_buf[0] - '0';
    if (mode == 0) {
        printf("\nStarting Menu based!!\n");
    } else {
        printf("\nPlease Enter 0 or 1\n");
        printf("\nExisting the App!!\n");
        exit(1);
    }
    rc = main_ctx.hal3appGetUserEvent();
    printf("Exiting application\n");
    return rc;
}

