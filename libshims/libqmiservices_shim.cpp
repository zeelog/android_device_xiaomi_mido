#define LOG_TAG "wcnss_shim"

#include <android-base/logging.h>

extern "C" void* dms_get_service_object_internal_v01(int, int, int);

extern "C" void* dms_get_service_object_shimshim_v01(int, int, int) {
	for (int i=0; i<255; ++i)
	{
		void* result = dms_get_service_object_internal_v01(1, i, 6);
		if (result)
		{
			LOG(INFO) << "Found magic value: " << i;
			return result;
		}
	}
	// Nothing found
	return 0;
}
