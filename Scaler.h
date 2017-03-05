#pragma once

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <getopt.h>

#include <exception>
#include <vector>
#include <queue>
#include <cstring>

#include <linux/videodev2.h> // V4L
#include <sys/mman.h>	// mmap
#include <stdint.h>


#define __user 
#include <uapi/drm/drm.h>
#include <uapi/drm/exynos_drm.h>
//#include <libdrm/exynos_drmif.h>
extern "C"
{
#include "exynos_fimg2d.h"
}


#include "Exception.h"



struct Int32x2
{
	uint32_t X;
	uint32_t Y;


	Int32x2()
		: X(0), Y(0)
	{
	}

	Int32x2(uint32_t x, uint32_t y)
		: X(x), Y(y)
	{
	}
};


class Scaler
{
	Int32x2 sourceSize;
	Int32x2 destSize;
	int fd;
	g2d_context* ctx = nullptr;


public:

	Scaler(Int32x2 sourceSize, Int32x2 destSize)
		: sourceSize(sourceSize), destSize(destSize)
	{
		//fd = drmOpen("exynos", 0);
		// /dev/dri/card0
		fd = open("/dev/dri/card0", O_RDWR | O_NONBLOCK, 0);
		if (fd < 0)
		{
			throw Exception("drmOpen failed.");
		}

#if 1
		ctx = g2d_init(fd);
		if (!ctx)
		{
			throw Exception("g2d_init failed.");
		}
#else

		drm_exynos_g2d_get_ver ver;

		int io = ioctl(fd, DRM_IOCTL_EXYNOS_G2D_GET_VER, &ver);
		if (io < 0) 
		{
			throw Exception("DRM_IOCTL_EXYNOS_G2D_GET_VER failed.");
		}

		printf("G2D version (%d.%d).\n", ver.major, ver.minor);
#endif
	}

	~Scaler()
	{
		g2d_fini(ctx);
		close(fd);
	}


	void Scale(void* sourceBufferY, void* sourceBufferUV,
		       void* destBufferY, void* destBufferUV)
	{
		g2d_image src_img;// = { 0 };
		g2d_image dst_img;// = { 0 };

		memset(&src_img, 0, sizeof(g2d_image));
		memset(&dst_img, 0, sizeof(g2d_image));


		//G2D_COLOR_FMT_L8
		src_img.width = sourceSize.X;
		src_img.height = sourceSize.Y;
		src_img.stride = sourceSize.X;
		src_img.buf_type = G2D_IMGBUF_USERPTR;
		src_img.color_mode = (e_g2d_color_mode)(G2D_COLOR_FMT_YCbCr420 | G2D_YCbCr_2PLANE);
		src_img.user_ptr[0].userptr = (long unsigned int)sourceBufferY;
		src_img.user_ptr[0].size = sourceSize.X * sourceSize.Y;
		src_img.user_ptr[1].userptr = (long unsigned int)sourceBufferUV;
		src_img.user_ptr[1].size = sourceSize.X * sourceSize.Y / 2;
		src_img.user_ptr[2].userptr = (long unsigned int)sourceBufferUV;
		src_img.user_ptr[2].size = sourceSize.X * sourceSize.Y / 2;

		dst_img.width = destSize.X;
		dst_img.height = destSize.Y;
		dst_img.stride = destSize.X;
		dst_img.buf_type = G2D_IMGBUF_USERPTR;
		dst_img.color_mode = (e_g2d_color_mode)(G2D_COLOR_FMT_YCbCr420 | G2D_YCbCr_2PLANE);
		dst_img.user_ptr[0].userptr = (long unsigned int)destBufferY;
		dst_img.user_ptr[0].size = destSize.X * destSize.Y;
		dst_img.user_ptr[1].userptr = (long unsigned int)destBufferUV;
		dst_img.user_ptr[1].size = destSize.X * destSize.Y / 2;
		dst_img.user_ptr[2].userptr = (long unsigned int)destBufferUV;
		dst_img.user_ptr[2].size = destSize.X * destSize.Y / 2;

#if 1
		int ret = g2d_copy_with_scale(ctx, &src_img, &dst_img,
			0, 0, sourceSize.X, sourceSize.Y,
			0, 0, destSize.X, destSize.Y,
			0);
		if (ret < 0)
		{
			//fprintf("errno=%d", errno);
			throw Exception("g2d_copy_with_scale failed.");
		}

		g2d_exec(ctx);
#endif
	}
};
