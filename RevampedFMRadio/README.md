<p align="center">
    <img src="https://user-images.githubusercontent.com/28353279/203109894-5c5cb86c-a882-4c0d-baab-24ba9a417082.svg" width="30%" />
</p>
<h2 align="center"><a href="https://github.com/iusmac/RevampedFMRadio">RevampedFMRadio</a></h2>

RevampedFMRadio app is a resurrection of the old but gold _AOSP FMRadio_ app actively developed for Android 5.0 (API level 21). On the internet, you may find different variations and implementations for QCOM and MTK SoCs. Here we're using latest source codebase from LineageOS 18.1 (Android 11).

LineageOS dropped _AOSP FMRadio_ in favor of _FMApp2_ (_com.caf.fmradio_) starting from LineageOS 19.0 (Android 12) release. Despite _FMApp2_ has a "freshy" codebase and API level 31 support, it doesn't work well in general (randomly stops & freezes) and provides a poor overall UI/UX.

_Codebase:_ https://github.com/LineageOS/android_packages_apps_FMRadio

AOSP FMRadio | RevampedFMRadio
:-----------:|:--------------:
<img src="https://user-images.githubusercontent.com/28353279/203124968-91be1ac2-4a10-4df6-a975-36a401b2c83b.png" width="20%" /> <img src="https://user-images.githubusercontent.com/28353279/203125260-69cb7932-42bc-44c0-8289-66121b1777f1.png" width="20%" /> <img src="https://user-images.githubusercontent.com/28353279/203126454-33b3268f-310a-40bc-a439-189075be0f11.png" width="20%" /> <img src="https://user-images.githubusercontent.com/28353279/203126834-0b2478b6-2b12-4ced-ab03-e32214f8a7bb.png" width="20%" /> | <img src="https://user-images.githubusercontent.com/28353279/203125070-e6fb38e9-306c-4b81-ab75-a2739d519f77.png" width="20%" /> <img src="https://user-images.githubusercontent.com/28353279/203125402-b5c1eefa-0bed-486f-ba05-449d78d9c410.png" width="20%" /> <img src="https://user-images.githubusercontent.com/28353279/203126610-d3aa70d3-3615-494c-b827-754ae6794fef.png" width="20%" /> <img src="https://user-images.githubusercontent.com/28353279/203127448-c96a5477-e301-4cc1-9e81-b29a9f9a89ca.png" width="20%" />

**What's done** (_click to expand spoilers_)**:**
1. <details><summary>Wireless (no headset) mode</summary>

   _Added support for wireless support. The RevampedFMRadio can be started without headset plugged in, instead we force use of built-in short antenna by default. Most local frequencies will work fine, but to get better quality and detailed <a href="https://en.wikipedia.org/wiki/Radio_Data_System">RDS</a> data, a long antenna (headset or just a jack cable) may be required._
   </details>
2. <details><summary>MPEG audio codec for recordings</summary>

   _Switched to MPEG (.mp3) audio codec for recordings. This way we can play the recording in system apps (Music app, File Manager etc.). Previously, the 3GPP (.3gpp) audio codec was used, which doesn't seems to be well supported._
   </details>
3. <details><summary>"<em>Recordings</em>" as saving directory</summary>

   _Changed saving directory for recordings to "Recordings". This is the proper place on Android 12+. Playlists (.m3u8) files will be stored in the "Music" directory as before._
   </details>
4. <details><summary>Dark mode support</summary>

   _RevampedFMRadio app will by default follow system theme in all activities._
   </details>
5. <details><summary>"<em>Material You</em>" color palette</summary>

   _Implemented "Material You" color palette introduced in Android 12. RevampedFMRadio app will by default follow system accent colors in all activities._
   </details>
6. <details><summary>Revamped UI</summary>

   - _redesigned favorite tiles_
   - _rounded corners like in QS_
   - _updated icons etc._
   </details>
7. <details><summary>Better UX</summary>

   _Improved favorites scrolling, playlist managing and many other small things._
   </details>
8. <details><summary>Grid view layout on the station list</summary>

   _Replaced list view with grid view on station list activity. The station list now follows same layout as the favorites._
   </details>
9. <details><summary>File size when recording</summary>

   _Added displaying in real-time of the recording file size on the recording activity._
   </details>
10. <details><summary>Station total</summary>

    _Added displaying of the total of known stations on the station list activity._
    </details>
11. <details><summary>Media player notification</summary>

    _Converted simple notification into a media player notification. The "stop" button was replaced with a play/pause button._
    </details>
12. <details><summary>Media session support</summary>

    - _Full metadata support_

      _This way the Android OS will treat the RevampedFMRadio app as a music source. It will allow to the user to apply sound effects (Dirac, Dolby Atmos) on the output audio._
    - _Full headset multimedia controls support_

      _Headset/headphones and other devices can now pause/resume the playback and jump/skip a station. Classic headsets with only one (hook) button, must press the hook button **twice** to jump to the next station and **three** times to jump to the previous station._
    </details>
13. Android 13 themed icons support
14. <details><summary>Fixed <a href="https://en.wikipedia.org/wiki/Radio_Data_System">RDS</a> data retrieving (<b>Qualcomm SoC only</b>)</summary>

    _This essential feature was broken on QCOM SoCs for years even on official LineageOS releases._
    </details>
15. <details><summary>Auto power save switch on SoC (<b>Qualcomm SoC only</b>)</summary>

    _The driver will be switched to low power mode to improve battery life once the device enters idle state (screen off), as we know for a fact that we will have fewer interruptions._
    </details>

## Installation
1. Clone the branch that corresponds to your device SoC, for example:
    - Use as an <em>in-tree</em> package within the device tree (<em>Recommended</em>):
        ```Console
        git clone --depth=1 -b qcom https://github.com/iusmac/RevampedFMRadio.git
        ```
    - Use as a project via [Local Manifests](https://gerrit.googlesource.com/git-repo/+/master/docs/manifest-format.md#Local-Manifests) (<em>Not recommended</em>):
        ```xml
        <remote name="iusmac" fetch="https://github.com/iusmac" revision="qcom" />
        <project path="packages/apps/RevampedFMRadio" name="RevampedFMRadio" remote="iusmac" />
        ```
        **NOTE:** there's a high chance of getting something broken if you always fetch upstream changes. It's recommended to go with <em>in-tree</em> package within your device tree, and from time to time manually merge and check upstream changes.

2. Add app to `device.mk`:
```Makefile
PRODUCT_PACKAGES += \
    RevampedFMRadio
```
**Note**: _RevampedFMRadio_ package will override _FMRadio_ package if the ROM sources still ship it.

<details><summary><h4>Qualcomm SoC-specific part</h4></summary>

3. Add JNI library to `device.mk`:
```Makefile
PRODUCT_PACKAGES += \
    libqcomfmjni
```
4. Make sure you have `vendor.qcom.bluetooth.soc` prop in your <em>vendor.prop</em> file.
   You may already have something similar, like `vendor.bluetooth.soc`, but it's legacy. Rename if proprietary blobs support new prop name or duplicate the value using new prop name to ensure RevampedFMRadio can properly comunicate to your device's Bluetooth SoC.
5. Allow app to read vendor properties mentioned in the previous step:
```Console
# sepolicy/vendor/system_app.te
get_prop(system_app, vendor_bluetooth_prop)
```
</details>
<details><summary><h4>MediaTek SoC-specific part</h4></summary></summary>

3. Add JNI library to `device.mk`:
```Makefile
PRODUCT_PACKAGES += \
    libmtkfmjni
```
</details>

## Contributing
Contributions are welcome! Fixes and improvements for different SoCs are highly appreciated. Share your changes via PR!

## Credits
- [iusmac (Max)](https://github.com/iusmac)
