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

#include "Exception.h"
#include "BufferMapping.h"


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
	static const int BUFFER_COUNT = 1;
	const char* decoderName = "/dev/video5";

	Int32x2 sourceSize;
	Int32x2 destSize;
	int fd;
	BufferMapping inputBuffer;
	BufferMapping outputBuffer;



	void RequestBuffer(int v4lBufferType, BufferMapping* bufferMappingOut)
	{
		int mfc_fd = fd;

		// Request buffers
		v4l2_requestbuffers requestBuffers = { 0 };
		requestBuffers.count = 1;
		requestBuffers.type = v4lBufferType; //V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		requestBuffers.memory = V4L2_MEMORY_MMAP;

		int io = ioctl(mfc_fd, VIDIOC_REQBUFS, &requestBuffers);
		if (io < 0)
		{
			throw Exception("VIDIOC_REQBUFS failed.");
		}

		if (requestBuffers.count > BUFFER_COUNT)
		{
			throw Exception("too many buffers.");
		}
		else
		{
			fprintf(stderr, "CreateInputBuffers: requestBuffers.count=%d\n", requestBuffers.count);
		}


		// Map buffers
		//BufferMapping bufferMappings[requestBuffers.count] = { 0 };
		//for (int i = 0; i < requestBuffers.count; ++i)
		{
			v4l2_plane planes[VIDEO_MAX_PLANES];

			v4l2_buffer buffer = { 0 };
			buffer.type = requestBuffers.type;
			buffer.memory = V4L2_MEMORY_MMAP;
			buffer.index = 0;
			buffer.m.planes = planes;
			buffer.length = 2;

			io = ioctl(mfc_fd, VIDIOC_QUERYBUF, &buffer);
			if (io < 0)
			{
				throw Exception("VIDIOC_QUERYBUF failed.");
			}

			BufferMapping mapping = { 0 };

			mapping.Length0 = buffer.m.planes[0].length;
			mapping.Start0 = mmap(NULL, mapping.Length0,
				PROT_READ | PROT_WRITE, /* recommended */
				MAP_SHARED,             /* recommended */
				mfc_fd,
				buffer.m.planes[0].m.mem_offset);
			if (mapping.Start0 == MAP_FAILED)
			{
				throw Exception("mmap 0 failed.");
			}

			mapping.Length1 = buffer.m.planes[1].length;
			mapping.Start1 = mmap(NULL, mapping.Length1,
				PROT_READ | PROT_WRITE, /* recommended */
				MAP_SHARED,             /* recommended */
				mfc_fd,
				buffer.m.planes[1].m.mem_offset);
			if (mapping.Start1 == MAP_FAILED)
			{
				throw Exception("mmap 1 failed.");
			}

			*bufferMappingOut = mapping;
		}
	}

	void EnumFormats(uint32_t v4l2BufType)
	{
		int io;
		int mfc_fd = fd;

		v4l2_fmtdesc formatDesc = { 0 };
		formatDesc.type = v4l2BufType; //V4L2_BUF_TYPE_VIDEO_CAPTURE;

		fprintf(stderr, "Supported formats:\n");
		while (true)
		{
			io = ioctl(mfc_fd, VIDIOC_ENUM_FMT, &formatDesc);
			if (io < 0)
			{
				//printf("VIDIOC_ENUM_FMT failed.\n");
				break;
			}

			fprintf(stderr, "\tdescription = %s, pixelformat=0x%x\n", formatDesc.description, formatDesc.pixelformat);


			v4l2_frmsizeenum formatSize = { 0 };
			formatSize.pixel_format = formatDesc.pixelformat;

			while (true)
			{
				io = ioctl(mfc_fd, VIDIOC_ENUM_FRAMESIZES, &formatSize);
				if (io < 0)
				{
					//printf("VIDIOC_ENUM_FRAMESIZES failed.\n");
					break;
				}

				fprintf(stderr, "\t\twidth = %d, height = %d\n", formatSize.discrete.width, formatSize.discrete.height);


				v4l2_frmivalenum frameInterval = { 0 };
				frameInterval.pixel_format = formatSize.pixel_format;
				frameInterval.width = formatSize.discrete.width;
				frameInterval.height = formatSize.discrete.height;

				while (true)
				{
					io = ioctl(mfc_fd, VIDIOC_ENUM_FRAMEINTERVALS, &frameInterval);
					if (io < 0)
					{
						//printf("VIDIOC_ENUM_FRAMEINTERVALS failed.\n");
						break;
					}

					fprintf(stderr, "\t\t\tnumerator = %d, denominator = %d\n", frameInterval.discrete.numerator, frameInterval.discrete.denominator);
					++frameInterval.index;
				}


				++formatSize.index;
			}

			++formatDesc.index;
		}
	}


public:
	Scaler(Int32x2 sourceSize, Int32x2 destSize)
		: sourceSize(sourceSize), destSize(destSize)
	{
		fd = open(decoderName, O_RDWR, 0);
		if (fd < 0)
		{
			throw Exception("Failed to open scaler");
		}


		// Check device capabilities
		v4l2_capability cap = { 0 };

		int ret = ioctl(fd, VIDIOC_QUERYCAP, &cap);
		if (ret != 0)
		{
			throw Exception("VIDIOC_QUERYCAP failed.");
		}

		if ((cap.capabilities & V4L2_CAP_VIDEO_M2M_MPLANE) == 0 ||
			(cap.capabilities & V4L2_CAP_STREAMING) == 0)
		{
			printf("V4L2_CAP_VIDEO_M2M_MPLANE=%d\n", (cap.capabilities & V4L2_CAP_VIDEO_M2M_MPLANE) != 0);
			printf("V4L2_CAP_STREAMING=%d\n", (cap.capabilities & V4L2_CAP_STREAMING) != 0);

			throw Exception("Insufficient capabilities of device.");
		}

#if 0
		fprintf(stderr, "V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE formats:\n");
		EnumFormats(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);

		fprintf(stderr, "-------------------------------------------\n");

		fprintf(stderr, "V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE formats:\n");
		EnumFormats(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
#endif



		// Set input properties
		v4l2_format format = { 0 };
		format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		format.fmt.pix_mp.width = sourceSize.X;
		format.fmt.pix_mp.height = sourceSize.Y;
		format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12M;
		format.fmt.pix_mp.num_planes = 2;

		int io = ioctl(fd, VIDIOC_S_FMT, &format);
		if (io < 0)
		{
			throw Exception("VIDIOC_S_FMT failed.");
		}

		fprintf(stderr, "Scaler input v4l2_format: width=%d, height=%d, pixelformat=0x%x\n",
			format.fmt.pix.width, format.fmt.pix.height, format.fmt.pix.pixelformat);

#if 0
		// Request input buffers
		v4l2_requestbuffers requestBuffers = { 0 };
		requestBuffers.count = BUFFER_COUNT;
		requestBuffers.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		requestBuffers.memory = V4L2_MEMORY_USERPTR;

		io = ioctl(fd, VIDIOC_REQBUFS, &requestBuffers);
		if (io < 0)
		{
			throw Exception("VIDIOC_REQBUFS failed.");
		}

		if (requestBuffers.count > BUFFER_COUNT)
		{
			throw Exception("too many buffers.");
		}
		else
		{
			fprintf(stderr, "Create input buffers: requestBuffers.count=%d\n", requestBuffers.count);
		}
#endif
		RequestBuffer(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, &inputBuffer);


		// Set output settings
		format = { 0 };
		format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		format.fmt.pix_mp.width = destSize.X;
		format.fmt.pix_mp.height = destSize.Y;
		format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12M;
		format.fmt.pix_mp.num_planes = 2;

		io = ioctl(fd, VIDIOC_S_FMT, &format);
		if (io < 0)
		{
			throw Exception("VIDIOC_S_FMT failed.");
		}

		fprintf(stderr, "Scaler output v4l2_format: width=%d, height=%d, pixelformat=0x%x\n",
			format.fmt.pix.width, format.fmt.pix.height, format.fmt.pix.pixelformat);

#if 0
		// Request output buffers
		requestBuffers = { 0 };
		requestBuffers.count = BUFFER_COUNT;
		requestBuffers.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		requestBuffers.memory = V4L2_MEMORY_USERPTR;

		io = ioctl(fd, VIDIOC_REQBUFS, &requestBuffers);
		if (io < 0)
		{
			throw Exception("VIDIOC_REQBUFS failed.");
		}

		if (requestBuffers.count > BUFFER_COUNT)
		{
			throw Exception("too many buffers.");
		}
		else
		{
			fprintf(stderr, "Create output buffers: requestBuffers.count=%d\n", requestBuffers.count);
		}
#endif

		RequestBuffer(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, &outputBuffer);



		v4l2_plane planes[VIDEO_MAX_PLANES];

		v4l2_buffer buffer = { 0 };
		buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		buffer.memory = V4L2_MEMORY_MMAP;
		buffer.index = 0;
		buffer.m.planes = planes;
		buffer.length = 2;

		io = ioctl(fd, VIDIOC_QUERYBUF, &buffer);
		if (io < 0)
		{
			throw Exception("DBG-A00: VIDIOC_QUERYBUF failed.");
		}

		// Queue buffers
		io = ioctl(fd, VIDIOC_QBUF, &buffer);
		if (io != 0)
		{
			throw Exception("DBG-A01: VIDIOC_QBUF failed.");
		}


		// Start output stream
		int val = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

		ret = ioctl(fd, VIDIOC_STREAMON, &val);
		if (ret != 0)
		{
			throw Exception("DBG-A02: VIDIOC_STREAMON failed.");
		}

		val = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

		ret = ioctl(fd, VIDIOC_STREAMON, &val);
		if (ret != 0)
		{
			throw Exception("DBG-A03: VIDIOC_STREAMON failed.");
		}
	}

	~Scaler()
	{
		// Stop output stream
		int val = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

		int ret = ioctl(fd, VIDIOC_STREAMOFF, &val);
		if (ret != 0)
		{
			throw Exception("VIDIOC_STREAMOFF failed.");
		}

		// Stop input stream
		val = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

		ret = ioctl(fd, VIDIOC_STREAMOFF, &val);
		if (ret != 0)
		{
			throw Exception("VIDIOC_STREAMOFF failed.");
		}


		close(fd);
	}


	void Scale(void* sourceBufferY, void* sourceBufferUV,
		       void* destBufferY, void* destBufferUV)
	{
		int io;


		// Get the buffer
		v4l2_plane planes[VIDEO_MAX_PLANES];

		v4l2_buffer buffer = { 0 };
		buffer.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		buffer.memory = V4L2_MEMORY_MMAP;
		buffer.index = 0;
		buffer.m.planes = planes;
		buffer.length = 2;

		io = ioctl(fd, VIDIOC_QUERYBUF, &buffer);
		if (io < 0)
		{
			throw Exception("DBG00: VIDIOC_QUERYBUF failed.");
		}

		// Copy data
		memcpy(inputBuffer.Start0, sourceBufferY, sourceSize.X * sourceSize.Y);
		buffer.m.planes[0].bytesused = sourceSize.X * sourceSize.Y;

		memcpy(inputBuffer.Start1, sourceBufferUV, sourceSize.X * sourceSize.Y / 2);
		buffer.m.planes[1].bytesused = sourceSize.X * sourceSize.Y / 2;


		// Queue buffer
		io = ioctl(fd, VIDIOC_QBUF, &buffer);
		if (io != 0)
		{
			throw Exception("DBG01: VIDIOC_QBUF failed.");
		}

		// Get the buffer back
		io = ioctl(fd, VIDIOC_DQBUF, &buffer);
		if (io != 0)
		{
			throw Exception("DBG02: VIDIOC_DQBUF failed.");
		}

		//if (!streamActive)
		//{
		//	// Start input stream
		//	int val = (int)V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

		//	ret = ioctl(mfc_fd, VIDIOC_STREAMON, &val);
		//	if (ret != 0)
		//	{
		//		throw Exception("VIDIOC_STREAMON failed.");
		//	}
		//	streamActive = true;
		//}

		// Wait for result
		buffer = { 0 };
		buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		buffer.memory = V4L2_MEMORY_MMAP;
		buffer.m.planes = planes;
		buffer.length = 2;

		io = ioctl(fd, VIDIOC_DQBUF, &buffer);
		if (io != 0)
		{
			throw Exception("DBG03: VIDIOC_DQBUF failed.");
		}


		// Copy data
		memcpy(destBufferY, outputBuffer.Start0, destSize.X * destSize.Y);
		memcpy(destBufferUV, outputBuffer.Start1, destSize.X * destSize.Y / 2);


		// Re-queue buffer
		io = ioctl(fd, VIDIOC_QBUF, &buffer);
		if (io != 0)
		{
			throw Exception("DBG04: VIDIOC_QBUF failed.");
		}
	

#if 0
		v4l2_selection selection = { 0 };

		//
		//V4L2_SEL_TGT_COMPOSE_DEFAULT
		selection.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		selection.target = V4L2_SEL_TGT_COMPOSE;
		selection.r.left = 0;
		selection.r.top = 0;
		selection.r.width = sourceSize.X;
		selection.r.height = sourceSize.Y;
		io = ioctl(fd, VIDIOC_S_SELECTION, &selection);
		if (io < 0)
		{
			throw Exception("VIDIOC_S_SELECTION failed.");
		}

		//V4L2_SEL_TGT_CROP_DEFAULT
		selection.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		selection.target = V4L2_SEL_TGT_CROP;

		io = ioctl(fd, VIDIOC_S_SELECTION, &selection);
		if (io < 0)
		{
			throw Exception("VIDIOC_S_SELECTION failed.");
		}

		//
		//V4L2_SEL_TGT_COMPOSE_DEFAULT
		selection.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		selection.target = V4L2_SEL_TGT_COMPOSE;
		selection.r.width = destSize.X;
		selection.r.height = destSize.Y;
		io = ioctl(fd, VIDIOC_S_SELECTION, &selection);
		if (io < 0)
		{
			throw Exception("VIDIOC_S_SELECTION failed.");
		}

		//V4L2_SEL_TGT_CROP_DEFAULT
		selection.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		selection.target = V4L2_SEL_TGT_CROP;

		io = ioctl(fd, VIDIOC_S_SELECTION, &selection);
		if (io < 0)
		{
			throw Exception("VIDIOC_S_SELECTION failed.");
		}


		// test
		selection = { 0 };
		selection.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		selection.target = V4L2_SEL_TGT_COMPOSE;

		io = ioctl(fd, VIDIOC_G_SELECTION, &selection);
		if (io < 0)
		{
			throw Exception("VIDIOC_G_SELECTION failed.");
		}
#endif
#if 0
		v4l2_plane planes[2] = { 0 };

		v4l2_buffer buffer = { 0 };
		buffer.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		buffer.memory = V4L2_MEMORY_USERPTR;
		buffer.index = 0;
		buffer.m.planes = planes;
		buffer.length = 2;

		//io = ioctl(fd, VIDIOC_QUERYBUF, &buffer);
		//if (io < 0)
		//{
		//	throw Exception("VIDIOC_QUERYBUF failed.");
		//}

		//buffer.m.userptr = 0;

		buffer.m.planes[0].m.userptr = (long unsigned int)destBufferY;
		buffer.m.planes[0].length = destSize.X * destSize.Y;
		buffer.m.planes[0].bytesused = buffer.m.planes[0].length; //destSize.X * destSize.Y;
		buffer.m.planes[1].m.userptr = (long unsigned int)destBufferUV;
		buffer.m.planes[1].length = destSize.X * destSize.Y / 2;
		buffer.m.planes[1].bytesused = buffer.m.planes[1].length; //destSize.X * destSize.Y / 2;

		//io = ioctl(fd, VIDIOC_QBUF, &buffer);
		//if (io != 0)
		//{
		//	throw Exception("VIDIOC_QBUF failed.");
		//}
#endif

	}
};
