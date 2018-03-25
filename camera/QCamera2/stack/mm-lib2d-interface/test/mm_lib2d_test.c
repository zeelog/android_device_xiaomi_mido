/* Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
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

// System dependencies
#include <dlfcn.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/time.h>

// Camera dependencies
#include "img_buffer.h"
#include "mm_lib2d.h"


#define ENABLE_OUTPUT_DUMP 1
#define ALIGN4K 4032
#define ALIGN(a, b) (((a) + (b)) & ~(b))


/** DUMP_TO_FILE:
 *  @filename: file name
 *  @p_addr: address of the buffer
 *  @len: buffer length
 *
 *  dump the image to the file
 **/
#define DUMP_TO_FILE(filename, p_addr, len) ({ \
  size_t rc = 0; \
  FILE *fp = fopen(filename, "w+"); \
  if (fp) { \
    rc = fwrite(p_addr, 1, len, fp); \
    printf(" ] written size %zu \n",  __LINE__, len); \
    fclose(fp); \
  } else { \
    printf(" ] open %s failed \n",  __LINE__, filename); \
  } \
})

/** DUMP_TO_FILE2:
 *  @filename: file name
 *  @p_addr: address of the buffer
 *  @len: buffer length
 *
 *  dump the image to the file if the memory is non-contiguous
 **/
#define DUMP_TO_FILE2(filename, p_addr1, len1, p_addr2, len2) ({ \
  size_t rc = 0; \
  FILE *fp = fopen(filename, "w+"); \
  if (fp) { \
    rc = fwrite(p_addr1, 1, len1, fp); \
    rc = fwrite(p_addr2, 1, len2, fp); \
    printf(" ] written %zu %zu \n",  __LINE__, len1, len2); \
    fclose(fp); \
  } else { \
    printf(" ] open %s failed \n",  __LINE__, filename); \
  } \
})

/** img_lib_buffert
 * @ptr: handle to the imglib library
 * @img_buffer_get: function pointer to img_buffer_get
 * @img_buffer_release: function pointer to img_buffer_release
 * @img_buffer_cacheops: function pointer to img_buffer_cacheops
**/
typedef struct {
  void *ptr;
  int (*img_buffer_get)(img_buf_type_t type, int heapid, int8_t cached, int length,
    img_mem_handle_t *p_handle);
  int (*img_buffer_release)(img_mem_handle_t *p_handle);
  int (*img_buffer_cacheops)(img_mem_handle_t *p_handle, img_cache_ops_t ops,
  img_mem_alloc_type_t mem_alloc_type);
} img_lib_buffert;

/** input_yuv_data
 * @filename: input test filename
 * @format: format of the input yuv frame
 * @wdith: wdith of the input yuv frame
 * @height: height of the input yuv frame
 * @stride: stride of the input yuv frame
 * @offset: offset to the yuv data in the input file
**/
typedef struct input_yuv_data_t {
  char filename[512];
  cam_format_t format;
  int32_t wdith;
  int32_t height;
  int32_t stride;
  int32_t offset;
} input_yuv_data;

input_yuv_data input_nv21[] = {
  {"sample0_768x512.yuv",                             CAM_FORMAT_YUV_420_NV21, 768,  512,  768,  0},
  {"sample1_3200x2400.yuv",                           CAM_FORMAT_YUV_420_NV21, 3200, 2400, 3200, 0},
  {"sample2_1920x1080.yuv",                           CAM_FORMAT_YUV_420_NV21, 1920, 1080, 1920, 0},
  {"sample3_3200x2400.yuv",                           CAM_FORMAT_YUV_420_NV21, 3200, 2400, 3200, 0},
  {"sample4_4208x3120.yuv",                           CAM_FORMAT_YUV_420_NV21, 4208, 3120, 4208, 0},
  {"sample5_1984x2592.yuv",                           CAM_FORMAT_YUV_420_NV21, 1984, 2592, 1984, 0},
  {"sample6_4000_3000.yuv",                           CAM_FORMAT_YUV_420_NV21, 4000, 3000, 4000, 0},
  {"sample7_3200_2400.yuv",                           CAM_FORMAT_YUV_420_NV21, 3200, 2400, 3200, 0},
  {"sample8_3008_4000.yuv",                           CAM_FORMAT_YUV_420_NV21, 3008, 4000, 3008, 0},
  {"sample9_5312x2988.yuv",                           CAM_FORMAT_YUV_420_NV21, 5312, 2988, 5312, 0},
  {"sample10_4128x3096.yuv",                          CAM_FORMAT_YUV_420_NV21, 4128, 3096, 4128, 0},
  {"sample11_4208x3120.yuv",                          CAM_FORMAT_YUV_420_NV21, 4208, 3120, 4208, 0},
  {"sample12_3200x2400.yuv",                          CAM_FORMAT_YUV_420_NV21, 3200, 2400, 3200, 0},
  {"sample13_width_1080_height_1440_stride_1088.yuv", CAM_FORMAT_YUV_420_NV21, 1080, 1440, 1088, 0},
  {"sample14_width_1080_height_1920_stride_1088.yuv", CAM_FORMAT_YUV_420_NV21, 1080, 1920, 1088, 0},
  {"sample15_width_1944_height_2592_stride_1984.yuv", CAM_FORMAT_YUV_420_NV21, 1944, 2592, 1984, 0},
  {"sample16_width_3000_height_4000_stride_3008.yuv", CAM_FORMAT_YUV_420_NV21, 3000, 4000, 3008, 0},
  {"sample17_width_3120_height_4208_stride_3136.yuv", CAM_FORMAT_YUV_420_NV21, 3120, 4208, 3136, 0},
  {"sample18_width_3200_height_2400_stride_3200.yuv", CAM_FORMAT_YUV_420_NV21, 3200, 2400, 3200, 0},
  {"sample19_width_1944_height_2592_stride_1984.yuv", CAM_FORMAT_YUV_420_NV21, 1944, 2592, 1984, 0},
};

// assuming buffer format is always ARGB
void lib2d_dump_tga(void *addr, cam_format_t format, int width,
  int height, int stride, char *fname)
{
  int i, j;
  FILE *f;
  unsigned char *pb = (unsigned char *)addr;
  uint32_t *pd = (uint32_t *)addr;
  int bpp = 32;

  f = fopen(fname, "wb");
  if (f) {
    // header
    fprintf(f, "%c%c%c%c", 0, 0, 2, 0);
    fprintf(f, "%c%c%c%c", 0, 0, 0, 0);
    fprintf(f, "%c%c%c%c", 0, 0, 0, 0);
    fprintf(f, "%c%c%c%c", width & 0xff, width >> 8, height & 0xff, height >> 8);
    fprintf(f, "%c%c", bpp, 32);

    for (i = 0; i < height; i++) {
      for (j = 0; j < width; j++) {
        fprintf(f, "%c%c%c%c",
          pd[(i*stride>>2)+j] & 0xff,           // b
          (pd[(i*stride>>2)+j] >> 8) & 0xff,    // g
          (pd[(i*stride>>2)+j] >> 16) & 0xff,   // r
          (pd[(i*stride>>2)+j] >> 24) & 0xff);  // a
      }
    }
    fclose(f);
  }
}

/**
 * Function: lib2d_test_client_cb
 *
 * Description: Callback that is called on completion of requested job.
 *
 * Input parameters:
 *   userdata - App userdata
 *   jobid - job id that is finished execution
 *
 * Return values:
 *   MM_LIB2D_SUCCESS
 *   MM_LIB2D_ERR_GENERAL
 *
 * Notes: none
 **/
lib2d_error lib2d_test_client_cb(void *userdata, int jobid)
{
  printf("%s %d, jobid=%d \n",  __LINE__, jobid);
  return MM_LIB2D_SUCCESS;
}

/**
 * Function: lib2d_test_load_input_yuv_data
 *
 * Description: Loads yuv data from input file.
 *
 * Input parameters:
 *   fileName - input yuv filename
 *   offset - offset to the yuv data in the input file
 *   y_size - y plane size in input yuv file
 *   crcb_size - crcb plane size in input yuv file
 *   crcb_offset - crcb offset in the memory at
 *       which crcb data need to be loaded
 *   addr - y plane memory address where y plane
 *       data need to be loaded.
 *
 * Return values:
 *   MM_LIB2D_SUCCESS
 *   MM_LIB2D_ERR_GENERAL
 *
 * Notes: none
 **/
lib2d_error lib2d_test_load_input_yuv_data(char *fileName, int offset,
    int32_t y_size, int32_t crcb_size, int32_t crcb_offset,
    void *addr)
{
  size_t i;
  FILE  *fp       = 0;
  void  *y_ptr    = addr;
  void  *crcb_ptr = (uint8_t *)addr + crcb_offset;

  printf("y_ptr=%p, crcb_ptr=%p \n", y_ptr, crcb_ptr);

  fp = fopen(fileName, "rb");
  if(fp) {
    if(offset) {
      fseek(fp, offset, SEEK_SET);
    }
    i = fread(y_ptr, 1, y_size, fp);
    i = fread(crcb_ptr, 1, crcb_size, fp);

    fclose( fp );
  } else {
    printf("failed to open file %s \n", fileName);
    return MM_LIB2D_ERR_GENERAL;
  }

  return MM_LIB2D_SUCCESS;
}

/**
 * Function: lib2d_test_load_input_yuv_data
 *
 * Description: Loads yuv data from input file.
 *
 * Input parameters:
 *   fileName - input yuv filename
 *   offset - offset to the yuv data in the input file
 *   input_yuv_stride - y plane stride in input yuv file
 *   y_plane_stride - y plane stride in buffer memory
 *   height - height of yuv image
 *   crcb_offset - crcb offset in the memory at
 *       which crcb data need to be loaded
 *   addr - y plane memory address where y plane
 *       data need to be loaded.
 *
 * Return values:
 *   MM_LIB2D_SUCCESS
 *   MM_LIB2D_ERR_GENERAL
 *
 * Notes: none
 **/
lib2d_error lib2d_test_load_input_yuv_data_linebyline(char *fileName,
    int offset, int32_t input_yuv_stride, int32_t y_plane_stride,
    int32_t height, int32_t crcb_offset, void *addr)
{
  size_t i;
  FILE  *fp       = 0;
  void  *y_ptr    = addr;
  void  *crcb_ptr = (uint8_t *)addr + crcb_offset;

  printf("y_ptr=%p, crcb_ptr=%p \n", y_ptr, crcb_ptr);

  fp = fopen(fileName, "rb");
  if(fp) {
    if(offset) {
      fseek(fp, offset, SEEK_SET);
    }
    if (input_yuv_stride == y_plane_stride) {
      //load y plane
      i = fread(y_ptr, 1, (input_yuv_stride * height), fp);
      // load UV plane
      i = fread(crcb_ptr, 1, (input_yuv_stride * height / 2), fp);
    } else {
      int line = 0;
      // load Y plane
      for (line = 0;line < height; line++) {
        i = fread(y_ptr, 1, input_yuv_stride, fp);
        y_ptr = (void *)((uint8_t *)y_ptr + y_plane_stride);
      }
      for (line = 0;line < height; line++) {
        i = fread(crcb_ptr, 1, input_yuv_stride, fp);
        crcb_ptr = (void *)((uint8_t *)crcb_ptr + y_plane_stride);
      }
    }

    fclose( fp );
  } else {
    printf("failed to open file %s \n", fileName);
    return MM_LIB2D_ERR_GENERAL;
  }

  return MM_LIB2D_SUCCESS;
}

/**
 * Function: main
 *
 * Description: main function for execution
 *
 * Input parameters:
 *   argc - no.of input arguments
 *   argv - list of arguments
 *
 * Return values:
 *   0 on success
 *   -1 on failure
 *
 * Notes: none
 **/
int main(int32_t argc, const char * argv[])
{
  void            *lib2d_handle       = NULL;
  lib2d_error      lib2d_err          = MM_LIB2D_SUCCESS;
  mm_lib2d_buffer  src_buffer         = {0};
  mm_lib2d_buffer  dst_buffer         = {0};
  int8_t           ret                = IMG_SUCCESS;
  int32_t          width              = 0;
  int32_t          height             = 0;
  int32_t          input_yuv_stride   = 0;
  int32_t          stride             = 0;
  int32_t          y_plane_stride     = 0;
  int32_t          crcb_plane_stride  = 0;
  int32_t          y_plane_size       = 0;
  int32_t          y_plane_size_align = 0;
  int32_t          crcb_plane_size    = 0;
  int32_t          yuv_size           = 0;
  int32_t          rgb_size           = 0;
  img_mem_handle_t m_yuv_memHandle    = { 0 };
  img_mem_handle_t m_rgb_memHandle    = { 0 };
  char             filename_in[512]   = { 0 };
  char             filename_out[512]  = { 0 };
  char             filename_raw[512]  = { 0 };
  int32_t          offset             = 0;
  unsigned int     total_tests        = 1;
  cam_format_t     format             = CAM_FORMAT_YUV_420_NV21;
  unsigned int     index;
  const char      *filename;

  // Open Imglib library and get the function pointers for
  // buffer allocation, free, cacheops
  img_lib_buffert  img_lib;
  img_lib.ptr = dlopen("libmmcamera_imglib.so", RTLD_NOW);
  if (!img_lib.ptr) {
    printf("%s ERROR: couldn't dlopen libmmcamera_imglib.so: %s",
       dlerror());
    return -1;
  }

  /* Get function pointer for functions to allocate ion memory */
  *(void **)&img_lib.img_buffer_get =
      dlsym(img_lib.ptr, "img_buffer_get");
  *(void **)&img_lib.img_buffer_release =
      dlsym(img_lib.ptr, "img_buffer_release");
  *(void **)&img_lib.img_buffer_cacheops =
      dlsym(img_lib.ptr, "img_buffer_cacheops");

  /* Validate function pointers */
  if ((img_lib.img_buffer_get == NULL) ||
    (img_lib.img_buffer_release == NULL) ||
    (img_lib.img_buffer_cacheops == NULL)) {
    printf(" ERROR mapping symbols from libmmcamera_imglib.so");
    dlclose(img_lib.ptr);
    return -1;
  }

  lib2d_err = mm_lib2d_init(MM_LIB2D_SYNC_MODE, CAM_FORMAT_YUV_420_NV21,
    CAM_FORMAT_8888_ARGB, &lib2d_handle);
  if ((lib2d_err != MM_LIB2D_SUCCESS) || (lib2d_handle == NULL)) {
    return -1;
  }

  bool run_default = FALSE;

  if ( argc == 7) {
    filename         = argv[1];
    width            = (uint32_t)atoi(argv[2]);
    height           = (uint32_t)atoi(argv[3]);
    input_yuv_stride = (uint32_t)atoi(argv[4]);
    offset           = (uint32_t)atoi(argv[5]);
    format           = (uint32_t)atoi(argv[6]);
    run_default      = TRUE;
    printf("Running user provided conversion \n");
  }
  else {
    total_tests = sizeof(input_nv21)/sizeof(input_yuv_data);
    printf("usage: <binary> <filname> <width> <height> "
      "<stride> <offset> <format> \n");
  }

  for (index = 0; index < total_tests; index++)
  {
    if(run_default == FALSE) {
      filename         = input_nv21[index].filename;
      width            = input_nv21[index].wdith;
      height           = input_nv21[index].height;
      input_yuv_stride = input_nv21[index].stride;
      offset           = input_nv21[index].offset;
      format           = input_nv21[index].format;
    }

    snprintf(filename_in, 512, "/data/lib2d/input/%s", filename);
    snprintf(filename_out, 512, "/data/lib2d/output/%s.tga", filename);
    snprintf(filename_raw, 512, "/data/lib2d/output/%s.rgba", filename);

    printf("-----------------Running test=%d/%d------------------------- \n",
      index+1, total_tests);
    printf("filename=%s, full path=%s, width=%d, height=%d, stride=%d \n",
      filename, filename_in, width, height, stride);

    // Allocate NV12 buffer
    y_plane_stride     = ALIGN(width, 32);
    y_plane_size       = y_plane_stride * height;
    y_plane_size_align = ALIGN(y_plane_size, ALIGN4K);
    crcb_plane_stride  = y_plane_stride;
    crcb_plane_size    = crcb_plane_stride * height / 2;
    yuv_size           = y_plane_size_align + crcb_plane_size;
    ret = img_lib.img_buffer_get(IMG_BUFFER_ION_IOMMU, -1, TRUE,
          yuv_size, &m_yuv_memHandle);
    if (ret != IMG_SUCCESS) {
      printf(" ] Error, img buf get failed \n");
      goto deinit;
    }

    printf("%s %d yuv buffer properties : w=%d, h=%d, y_stride=%d, "
      "crcb_stride=%d, y_size=%d, crcb_size=%d, yuv_size=%d, "
      "crcb_offset=%d \n",
       __LINE__,
      width, height, y_plane_stride, crcb_plane_stride, y_plane_size,
      crcb_plane_size, yuv_size, y_plane_size_align);
    printf("%s %d yuv buffer properties : fd=%d, ptr=%p, size=%d \n",
       __LINE__, m_yuv_memHandle.fd, m_yuv_memHandle.vaddr,
      m_yuv_memHandle.length);

    // Allocate ARGB buffer
    stride   = width * 4;
    stride   = ALIGN(stride, 32);
    rgb_size = stride * height;
    ret = img_lib.img_buffer_get(IMG_BUFFER_ION_IOMMU, -1, TRUE,
          rgb_size, &m_rgb_memHandle);
    if (ret != IMG_SUCCESS) {
      printf(" ] Error, img buf get failed");
      img_lib.img_buffer_release(&m_yuv_memHandle);
      goto deinit;
    }

    printf("%s %d rgb buffer properties : w=%d, h=%d, stride=%d, size=%d \n",
       __LINE__, width, height, stride, rgb_size);
    printf("%s %d rgb buffer properties : fd=%d, ptr=%p, size=%d \n",
       __LINE__, m_rgb_memHandle.fd, m_rgb_memHandle.vaddr,
      m_rgb_memHandle.length);

#if 0
    lib2d_err = lib2d_test_load_input_yuv_data(filename_in, offset,
      (input_yuv_stride * height), (input_yuv_stride * height / 2), y_plane_size_align,
      m_yuv_memHandle.vaddr);
    if (lib2d_err != MM_LIB2D_SUCCESS) {
      printf(" ] Error loading the input buffer \n");
      goto release;
    }
#else
    lib2d_err = lib2d_test_load_input_yuv_data_linebyline(filename_in, offset,
      input_yuv_stride, y_plane_stride,height, y_plane_size_align,
      m_yuv_memHandle.vaddr);
    if (lib2d_err != MM_LIB2D_SUCCESS) {
      printf(" ] Error loading the input buffer \n");
      goto release;
    }
#endif
    // Setup source buffer
    src_buffer.buffer_type = MM_LIB2D_BUFFER_TYPE_YUV;
    src_buffer.yuv_buffer.fd      = m_yuv_memHandle.fd;
    src_buffer.yuv_buffer.format  = format;
    src_buffer.yuv_buffer.width   = width;
    src_buffer.yuv_buffer.height  = height;
    src_buffer.yuv_buffer.plane0  = m_yuv_memHandle.vaddr;
    src_buffer.yuv_buffer.stride0 = y_plane_stride;
    src_buffer.yuv_buffer.plane1  = (int8_t *)m_yuv_memHandle.vaddr +
                                    y_plane_size_align;
    src_buffer.yuv_buffer.stride1 = crcb_plane_stride;

    // Setup dst buffer
    dst_buffer.buffer_type = MM_LIB2D_BUFFER_TYPE_RGB;
    dst_buffer.rgb_buffer.fd     = m_rgb_memHandle.fd;
    dst_buffer.rgb_buffer.format = CAM_FORMAT_8888_ARGB;
    dst_buffer.rgb_buffer.width  = width;
    dst_buffer.rgb_buffer.height = height;
    dst_buffer.rgb_buffer.buffer = m_rgb_memHandle.vaddr;
    dst_buffer.rgb_buffer.stride = stride;

    img_lib.img_buffer_cacheops(&m_yuv_memHandle,
      IMG_CACHE_CLEAN_INV, IMG_INTERNAL);

    lib2d_err = mm_lib2d_start_job(lib2d_handle, &src_buffer, &dst_buffer,
      index, NULL, lib2d_test_client_cb, 0);
    if (lib2d_err != MM_LIB2D_SUCCESS) {
      printf(" ] Error in mm_lib2d_start_job \n");
      goto release;
    }

    img_lib.img_buffer_cacheops(&m_rgb_memHandle,
      IMG_CACHE_CLEAN_INV, IMG_INTERNAL);

#ifdef ENABLE_OUTPUT_DUMP
    // Dump output files
    // snprintf(filename_in, 512, "/data/lib2d/output/%s", filename);
    // DUMP_TO_FILE2(filename_in, src_buffer.yuv_buffer.plane0, y_plane_size, src_buffer.yuv_buffer.plane1, crcb_plane_size);
    // DUMP_TO_FILE(filename_raw, dst_buffer.rgb_buffer.buffer, rgb_size);
    printf("Dumping output file %s \n", filename_out);
    lib2d_dump_tga(dst_buffer.rgb_buffer.buffer, 1,
      width, height, stride, filename_out);
#endif

    img_lib.img_buffer_release(&m_rgb_memHandle);
    img_lib.img_buffer_release(&m_yuv_memHandle);
  }

  mm_lib2d_deinit(lib2d_handle);

  return 0;

release:
  img_lib.img_buffer_release(&m_rgb_memHandle);
  img_lib.img_buffer_release(&m_yuv_memHandle);
deinit:
  mm_lib2d_deinit(lib2d_handle);
  printf("%s %d some error happened, tests completed = %d/%d \n",
     __LINE__, index - 1, total_tests);
  return -1;
}


