package org.ifaa.android.manager;

import android.content.Context;
import android.content.Intent;
import android.net.wifi.WifiEnterpriseConfig;
import android.os.Build;
import android.os.Build.VERSION;
import android.os.HwBinder;
import android.os.HwBlob;
import android.os.HwParcel;
import android.os.IHwBinder;
import android.os.RemoteException;
import android.os.SystemProperties;
import android.util.Slog;
import java.util.ArrayList;
import java.util.Arrays;
import org.json.JSONObject;

public class IFAAManagerImpl extends IFAAManagerV3 {
    private static final String TAG = "IfaaManagerImpl";

    private static volatile IFAAManagerImpl INSTANCE = null;

    private static final int IFAA_TYPE_FINGER = 0x01;
    private static final int IFAA_TYPE_IRIS = 0x02;
    private static final int IFAA_TYPE_SENSOR_FOD = 0x10;

    private static final int ACTIVITY_START_SUCCESS = 0;
    private static final int ACTIVITY_START_FAILED = -1;

    private static final int CODE_PROCESS_CMD = 1;
    private static final String INTERFACE_DESCRIPTOR = "vendor.xiaomi.hardware.mlipay@1.0::IMlipayService";
    private static final String SERVICE_NAME = "vendor.xiaomi.hardware.mlipay@1.0::IMlipayService";

    private static final String seperate = ",";
    private String mDevModel = null;
    private IHwBinder mService;

    public static IFAAManagerV3 getInstance() {
        if (INSTANCE == null) {
            synchronized (IFAAManagerImpl.class) {
                if (INSTANCE == null) {
                    INSTANCE = new IFAAManagerImpl();
                }
            }
        }
        return INSTANCE;
    }

    private String initExtString() {
        String str = "";
        JSONObject location = new JSONObject();
        JSONObject fullView = new JSONObject();
        String str2 = SystemProperties.get("persist.sys.fp.fod.location.X_Y", "");
        String str3 = SystemProperties.get("persist.sys.fp.fod.size.width_height", "");
        try {
            if (validateVal(str2) && validateVal(str3)) {
                String[] split = str2.split(seperate);
                String[] split2 = str3.split(seperate);
                fullView.put("startX", Integer.parseInt(split[0]));
                fullView.put("startY", Integer.parseInt(split[1]));
                fullView.put("width", Integer.parseInt(split2[0]));
                fullView.put("height", Integer.parseInt(split2[1]));
                fullView.put("navConflict", true);
                location.put("type", 0);
                location.put("fullView", fullView);
                return location.toString();
            }
            Slog.e(TAG, "initExtString invalidate, xy:" + str2 + " wh:" + str3);
            return str;
        } catch (Exception e) {
            Slog.e(TAG, "Exception , xy:" + str2 + " wh:" + str3, e);
            return str;
        }
    }

    private boolean validateVal(String str) {
        return !"".equalsIgnoreCase(str) && str.contains(",");
    }

    public String getDeviceModel() {
        if (mDevModel == null) {
            mDevModel = Build.MANUFACTURER + "-" + Build.DEVICE;
        }
        Slog.i(TAG, "getDeviceModel devcieModel:" + mDevModel);
        return mDevModel;
    }

    public String getExtInfo(int authType, String keyExtInfo) {
        Slog.i(TAG, "getExtInfo:" + authType + WifiEnterpriseConfig.CA_CERT_ALIAS_DELIMITER + keyExtInfo);
        return initExtString();
    }

    public int getSupportBIOTypes(Context context) {
        int ifaaType = SystemProperties.getInt("persist.sys.ifaa", 0);
        String fpVendor = SystemProperties.get("persist.sys.fp.vendor", "");
        int supportBIOTypes = "none".equalsIgnoreCase(fpVendor) ? ifaaType & IFAA_TYPE_IRIS :
                ifaaType & (IFAA_TYPE_FINGER | IFAA_TYPE_IRIS);
        if ((supportBIOTypes & IFAA_TYPE_FINGER) == IFAA_TYPE_FINGER && sIsFod) {
            supportBIOTypes |= IFAA_TYPE_SENSOR_FOD;
        }
        return supportBIOTypes;
    }

    public int getVersion() {
        Slog.i(TAG, "getVersion sdk:" + VERSION.SDK_INT + " ifaaVer:" + sIfaaVer);
        return sIfaaVer;
    }

    public byte[] processCmdV2(Context context, byte[] data) {
        Slog.i(TAG, "processCmdV2 sdk:" + VERSION.SDK_INT);
        HwParcel hwParcel = new HwParcel();
        try {
            if (mService == null) {
                mService = HwBinder.getService(SERVICE_NAME, "default");
            }
            if (mService != null) {
                HwParcel hwParcel2 = new HwParcel();
                hwParcel2.writeInterfaceToken(INTERFACE_DESCRIPTOR);
                ArrayList arrayList = new ArrayList(Arrays.asList(HwBlob.wrapArray(data)));
                hwParcel2.writeInt8Vector(arrayList);
                hwParcel2.writeInt32(arrayList.size());
                mService.transact(CODE_PROCESS_CMD, hwParcel2, hwParcel, 0);
                hwParcel.verifySuccess();
                hwParcel2.releaseTemporaryStorage();
                ArrayList readInt8Vector = hwParcel.readInt8Vector();
                int size = readInt8Vector.size();
                byte[] result = new byte[size];
                for (int i = 0; i < size; i++) {
                    result[i] = ((Byte) readInt8Vector.get(i)).byteValue();
                }
                return result;
            }
        } catch (RemoteException e) {
            Slog.e(TAG, "transact failed. " + e);
        } finally {
            hwParcel.release();
        }
        Slog.e(TAG, "processCmdV2, return null");
        return null;
    }

    public void setExtInfo(int authType, String keyExtInfo, String valExtInfo) {
    }

    public int startBIOManager(Context context, int authType) {
        int res = ACTIVITY_START_FAILED;
        if (authType == IFAA_TYPE_FINGER) {
            Intent intent = new Intent("android.settings.SECURITY_SETTINGS");
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            context.startActivity(intent);
            res = ACTIVITY_START_SUCCESS;
        }
        Slog.i(TAG, "startBIOManager authType:" + authType + " res:" + res);
        return res;
    }
}
