/* Copyright (c) 2012-2016, The Linux Foundation. All rights reserved.
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
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

// Camera dependencies
#include "mm_camera_dbg.h"
#include "mm_camera_sock.h"

/*===========================================================================
 * FUNCTION   : mm_camera_socket_create
 *
 * DESCRIPTION: opens a domain socket tied to camera ID and socket type
 *  @cam_id   : camera ID
 *  @sock_type: socket type, TCP/UDP
 *
 * RETURN     : fd related to the domain socket
 *==========================================================================*/
int mm_camera_socket_create(int cam_id, mm_camera_sock_type_t sock_type)
{
    int socket_fd;
    mm_camera_sock_addr_t sock_addr;
    int sktype;
    int rc;

    switch (sock_type)
    {
      case MM_CAMERA_SOCK_TYPE_UDP:
        sktype = SOCK_DGRAM;
        break;
      case MM_CAMERA_SOCK_TYPE_TCP:
        sktype = SOCK_STREAM;
        break;
      default:
        LOGE("unknown socket type =%d", sock_type);
        return -1;
    }
    socket_fd = socket(AF_UNIX, sktype, 0);
    if (socket_fd < 0) {
        LOGE("error create socket fd =%d", socket_fd);
        return socket_fd;
    }

    memset(&sock_addr, 0, sizeof(sock_addr));
    sock_addr.addr_un.sun_family = AF_UNIX;
    snprintf(sock_addr.addr_un.sun_path,
             UNIX_PATH_MAX, "/data/vendor/qcam/cam_socket%d", cam_id);
    rc = connect(socket_fd, &sock_addr.addr, sizeof(sock_addr.addr_un));
    if (0 != rc) {
      close(socket_fd);
      socket_fd = -1;
      LOGE("socket_fd=%d %s ", socket_fd, strerror(errno));
    }

    LOGD("socket_fd=%d %s", socket_fd,
        sock_addr.addr_un.sun_path);
    return socket_fd;
}

/*===========================================================================
 * FUNCTION   : mm_camera_socket_close
 *
 * DESCRIPTION:  close domain socket by its fd
 *   @fd      : file descriptor for the domain socket to be closed
 *
 * RETURN     : none
 *==========================================================================*/
void mm_camera_socket_close(int fd)
{
    if (fd >= 0) {
      close(fd);
    }
}

/*===========================================================================
 * FUNCTION   : mm_camera_socket_sendmsg
 *
 * DESCRIPTION:  send msg through domain socket
 *   @fd      : socket fd
 *   @msg     : pointer to msg to be sent over domain socket
 *   @sendfd  : file descriptors to be sent
 *
 * RETURN     : the total bytes of sent msg
 *==========================================================================*/
int mm_camera_socket_sendmsg(
  int fd,
  void *msg,
  size_t buf_size,
  int sendfd)
{
    struct msghdr msgh;
    struct iovec iov[1];
    struct cmsghdr * cmsghp = NULL;
    char control[CMSG_SPACE(sizeof(int))];

    if (msg == NULL) {
      LOGD("msg is NULL");
      return -1;
    }
    memset(&msgh, 0, sizeof(msgh));
    msgh.msg_name = NULL;
    msgh.msg_namelen = 0;

    iov[0].iov_base = msg;
    iov[0].iov_len = buf_size;
    msgh.msg_iov = iov;
    msgh.msg_iovlen = 1;
    LOGD("iov_len=%llu",
            (unsigned long long int)iov[0].iov_len);

    msgh.msg_control = NULL;
    msgh.msg_controllen = 0;

    /* if sendfd is valid, we need to pass it through control msg */
    if( sendfd >= 0) {
      msgh.msg_control = control;
      msgh.msg_controllen = sizeof(control);
      cmsghp = CMSG_FIRSTHDR(&msgh);
      if (cmsghp != NULL) {
        LOGD("Got ctrl msg pointer");
        cmsghp->cmsg_level = SOL_SOCKET;
        cmsghp->cmsg_type = SCM_RIGHTS;
        cmsghp->cmsg_len = CMSG_LEN(sizeof(int));
        *((int *)CMSG_DATA(cmsghp)) = sendfd;
        LOGD("cmsg data=%d", *((int *) CMSG_DATA(cmsghp)));
      } else {
        LOGD("ctrl msg NULL");
        return -1;
      }
    }

    return sendmsg(fd, &(msgh), 0);
}

/*===========================================================================
 * FUNCTION   : mm_camera_socket_bundle_sendmsg
 *
 * DESCRIPTION:  send msg through domain socket
 *   @fd      : socket fd
 *   @msg     : pointer to msg to be sent over domain socket
 *   @sendfds : file descriptors to be sent
 *   @numfds  : num of file descriptors to be sent
 *
 * RETURN     : the total bytes of sent msg
 *==========================================================================*/
int mm_camera_socket_bundle_sendmsg(
  int fd,
  void *msg,
  size_t buf_size,
  int sendfds[CAM_MAX_NUM_BUFS_PER_STREAM],
  int numfds)
{
    struct msghdr msgh;
    struct iovec iov[1];
    struct cmsghdr * cmsghp = NULL;
    char control[CMSG_SPACE(sizeof(int) * numfds)];
    int *fds_ptr = NULL;

    if (msg == NULL) {
      LOGD("msg is NULL");
      return -1;
    }
    memset(&msgh, 0, sizeof(msgh));
    msgh.msg_name = NULL;
    msgh.msg_namelen = 0;

    iov[0].iov_base = msg;
    iov[0].iov_len = buf_size;
    msgh.msg_iov = iov;
    msgh.msg_iovlen = 1;
    LOGD("iov_len=%llu",
            (unsigned long long int)iov[0].iov_len);

    msgh.msg_control = NULL;
    msgh.msg_controllen = 0;

    /* if numfds is valid, we need to pass it through control msg */
    if (numfds > 0) {
      msgh.msg_control = control;
      msgh.msg_controllen = sizeof(control);
      cmsghp = CMSG_FIRSTHDR(&msgh);
      if (cmsghp != NULL) {
        cmsghp->cmsg_level = SOL_SOCKET;
        cmsghp->cmsg_type = SCM_RIGHTS;
        cmsghp->cmsg_len = CMSG_LEN(sizeof(int) * numfds);

        fds_ptr = (int*) CMSG_DATA(cmsghp);
        memcpy(fds_ptr, sendfds, sizeof(int) * numfds);
      } else {
        LOGE("ctrl msg NULL");
        return -1;
      }
    }

    return sendmsg(fd, &(msgh), 0);
}

/*===========================================================================
 * FUNCTION   : mm_camera_socket_recvmsg
 *
 * DESCRIPTION:  receive msg from domain socket.
 *   @fd      : socket fd
 *   @msg     : pointer to mm_camera_sock_msg_packet_t to hold incoming msg,
 *              need be allocated by the caller
 *   @buf_size: the size of the buf that holds incoming msg
 *   @rcvdfd  : pointer to hold recvd file descriptor if not NULL.
 *
 * RETURN     : the total bytes of received msg
 *==========================================================================*/
int mm_camera_socket_recvmsg(
  int fd,
  void *msg,
  uint32_t buf_size,
  int *rcvdfd)
{
    struct msghdr msgh;
    struct iovec iov[1];
    struct cmsghdr *cmsghp = NULL;
    char control[CMSG_SPACE(sizeof(int))];
    int rcvd_fd = -1;
    int rcvd_len = 0;

    if ( (msg == NULL) || (buf_size <= 0) ) {
      LOGE("msg buf is NULL");
      return -1;
    }

    memset(&msgh, 0, sizeof(msgh));
    msgh.msg_name = NULL;
    msgh.msg_namelen = 0;
    msgh.msg_control = control;
    msgh.msg_controllen = sizeof(control);

    iov[0].iov_base = msg;
    iov[0].iov_len = buf_size;
    msgh.msg_iov = iov;
    msgh.msg_iovlen = 1;

    if ( (rcvd_len = recvmsg(fd, &(msgh), 0)) <= 0) {
      LOGE("recvmsg failed");
      return rcvd_len;
    }

    LOGD("msg_ctrl %p len %zd", msgh.msg_control,
        msgh.msg_controllen);

    if( ((cmsghp = CMSG_FIRSTHDR(&msgh)) != NULL) &&
        (cmsghp->cmsg_len == CMSG_LEN(sizeof(int))) ) {
      if (cmsghp->cmsg_level == SOL_SOCKET &&
        cmsghp->cmsg_type == SCM_RIGHTS) {
        LOGD("CtrlMsg is valid");
        rcvd_fd = *((int *) CMSG_DATA(cmsghp));
        LOGD("Receieved fd=%d", rcvd_fd);
      } else {
        LOGE("Unexpected Control Msg. Line=%d");
      }
    }

    if (rcvdfd) {
      *rcvdfd = rcvd_fd;
    }

    return rcvd_len;
}
