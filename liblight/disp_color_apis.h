#ifndef __DISP_COLOR_APIS_H__
#define __DISP_COLOR_APIS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <sys/types.h>
#include <cutils/log.h>

typedef uint64_t DISPAPI_HANDLE;


/*=========================================================================
FUNCTION
  disp_api_init()

DESCRIPTION
  This API initializes the display API library.

  This function must be called before calling any display APIs.
  The function returns a context handle that must be used on all subsequent
  calls.

  hctx  -- returns context handle on a successful call
  flags -- Reserved

RETURN VALUE
  ZERO (SUCCESS)- success.
  Negative Value - error\failure
=========================================================================*/

int32_t disp_api_init(DISPAPI_HANDLE *hctx, uint32_t flags);

/*=========================================================================
FUNCTION
  disp_api_set_panel_brightness_level_ext()

DESCRIPTION
  This API adjusts the backlight brightness.

  hctx     -- Context handle.
  disp_id  -- Display ID type
  level -- Color balance adjustement, the larger the value the warmer the color
  flags    -- Reserved

RETURN VALUE
  ZERO (SUCCESS)- success.
  Negative Value - error\failure
=========================================================================*/
int32_t disp_api_set_panel_brightness_level_ext(DISPAPI_HANDLE hctx, uint32_t disp_id, int32_t level,
                                          uint32_t flags);

#ifdef __cplusplus
}
#endif

#endif  // __DISP_COLOR_APIS_H__
