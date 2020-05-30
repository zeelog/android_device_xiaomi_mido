/*
 * Copyright (C) 2015 The CyanogenMod Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _BDROID_BUILDCFG_H
#define _BDROID_BUILDCFG_H

#define BTM_DEF_LOCAL_NAME   "Redmi Note 4"
#define MAX_ACL_CONNECTIONS    16
#define MAX_L2CAP_CHANNELS    32
#define BLE_VND_INCLUDED   TRUE
#define GATT_MAX_PHY_CHANNEL  10

// Wide-band speech support
#define BTIF_HF_WBS_PREFERRED TRUE

// skips conn update at conn completion
#define BT_CLEAN_TURN_ON_DISABLED 1

/* Disable enhanced sco commands */
#define BTM_SCO_ENHANCED_SYNC_ENABLED FALSE

#define AVDT_NUM_SEPS 35

#endif /* _BDROID_BUILDCFG_H */
