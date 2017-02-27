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


class Exception : public std::exception
{
public:
	Exception(const char* message)
		: std::exception()
	{
		fprintf(stderr, "%s\n", message);
	}

};


struct BufferMapping
{
	void* Start0;
	size_t Length0;
	void* Start1;
	size_t Length1;
};


class M2M
{
	static const int BUFFER_COUNT = 4;

	//[    2.236569] s5p-mfc 11000000.codec:: decoder registered as /dev/video10
	//[    2.305343] s5p-mfc 11000000.codec:: encoder registered as /dev/video11
	const char* decoderName = "/dev/video11";
	
	int mfc_fd;
	int width;
	int height;
	int fps;
	int bitrate;
	int gop;

	std::vector<BufferMapping> inputBuffers;
	std::vector<BufferMapping> outputBuffers;
	bool streamActive = false;
	std::queue<int> freeInputBuffers;


	void SetProfile()
	{
		//V4L2_CID_MPEG_VIDEO_H264_PROFILE = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH
		//V4L2_CID_MPEG_VIDEO_H264_LEVEL = V4L2_MPEG_VIDEO_H264_LEVEL_4_0

#if 1
		v4l2_ext_control ctrl[2] = { 0 };
		ctrl[0].id = V4L2_CID_MPEG_VIDEO_H264_PROFILE;
		ctrl[0].value = V4L2_MPEG_VIDEO_H264_PROFILE_MAIN;

		ctrl[1].id = V4L2_CID_MPEG_VIDEO_H264_LEVEL;
		ctrl[1].value = V4L2_MPEG_VIDEO_H264_LEVEL_4_0;

		v4l2_ext_controls ctrls = { 0 };
		ctrls.ctrl_class = V4L2_CTRL_CLASS_MPEG;
		ctrls.count = 2;
		ctrls.controls = ctrl;

		int io = ioctl(mfc_fd, VIDIOC_S_EXT_CTRLS, &ctrls);
		if (io != 0)
		{
			throw Exception("VIDIOC_S_EXT_CTRLS failed.");
		}
#endif

	}

	void SetBitrate(int value)
	{
		v4l2_ext_control ctrl[2] = { 0 };
		ctrl[0].id = V4L2_CID_MPEG_VIDEO_BITRATE;
		ctrl[0].value = value;

		ctrl[1].id = V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE;
		ctrl[1].value = 1;

		v4l2_ext_controls ctrls = { 0 };
		ctrls.ctrl_class = V4L2_CTRL_CLASS_MPEG;
		ctrls.count = 2;
		ctrls.controls = ctrl;

		int io = ioctl(mfc_fd, VIDIOC_S_EXT_CTRLS, &ctrls);
		if (io != 0)
		{
			throw Exception("VIDIOC_S_EXT_CTRLS failed.");
		}
	}

	void SetGroupOfPictures(int value)
	{
		v4l2_ext_control ctrl = { 0 };
		ctrl.id = V4L2_CID_MPEG_VIDEO_GOP_SIZE;
		ctrl.value = value;

		v4l2_ext_controls ctrls = { 0 };
		ctrls.ctrl_class = V4L2_CTRL_CLASS_MPEG;
		ctrls.count = 1;
		ctrls.controls = &ctrl;

		int io = ioctl(mfc_fd, VIDIOC_S_EXT_CTRLS, &ctrls);
		if (io != 0)
		{
			throw Exception("VIDIOC_S_EXT_CTRLS failed.");
		}
	}

	void ApplyInputSettings()
	{
		// Apply capture settings
		v4l2_format format = { 0 };
		format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		format.fmt.pix_mp.width = width;
		format.fmt.pix_mp.height = height;
		format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12M;
		format.fmt.pix_mp.num_planes = 2;

		int io = ioctl(mfc_fd, VIDIOC_S_FMT, &format);
		if (io < 0)
		{
			throw Exception("VIDIOC_S_FMT failed.");
		}

		fprintf(stderr, "v4l2_format: width=%d, height=%d, pixelformat=0x%x\n",
			format.fmt.pix.width, format.fmt.pix.height, format.fmt.pix.pixelformat);


		v4l2_streamparm streamParm = { 0 };
		streamParm.type = format.type;
		streamParm.parm.capture.timeperframe.numerator = 1;
		streamParm.parm.capture.timeperframe.denominator = fps;

		io = ioctl(mfc_fd, VIDIOC_S_PARM, &streamParm);
		if (io < 0)
		{
			throw Exception("VIDIOC_S_PARM failed.");
		}

		fprintf(stderr, "capture.timeperframe: numerator=%d, denominator=%d\n",
			streamParm.parm.capture.timeperframe.numerator,
			streamParm.parm.capture.timeperframe.denominator);


		SetProfile();

		SetBitrate(bitrate);
		
		SetGroupOfPictures(gop);
	}

	void CreateInputBuffers()
	{
		// Request buffers
		v4l2_requestbuffers requestBuffers = { 0 };
		requestBuffers.count = BUFFER_COUNT;
		requestBuffers.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
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
		for (int i = 0; i < requestBuffers.count; ++i)
		{
			v4l2_plane planes[VIDEO_MAX_PLANES];

			v4l2_buffer buffer = { 0 };
			buffer.type = requestBuffers.type;
			buffer.memory = V4L2_MEMORY_MMAP;
			buffer.index = i;
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

			inputBuffers.push_back(mapping);
			freeInputBuffers.push(i);

#if 0 // Dont queue input buffers, only output buffers

			// Queue buffers
			int ret = ioctl(mfc_fd, VIDIOC_QBUF, &buffer);
			if (ret != 0)
			{
				throw Exception("VIDIOC_QBUF failed.");
			}
#endif

		}
	}

	void ApplyOutputSettings()
	{
		// Apply capture settings
		v4l2_format format = { 0 };
		format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		format.fmt.pix_mp.width = width;
		format.fmt.pix_mp.height = height;
		format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;
		format.fmt.pix_mp.plane_fmt[0].sizeimage = width * height * 4;

		int io = ioctl(mfc_fd, VIDIOC_S_FMT, &format);
		if (io < 0)
		{
			throw Exception("VIDIOC_S_FMT failed.");
		}

		fprintf(stderr, "v4l2_format: width=%d, height=%d, pixelformat=0x%x\n",
			format.fmt.pix.width, format.fmt.pix.height, format.fmt.pix.pixelformat);

	}

	void CreateOutputBuffers()
	{
		// Request buffers
		v4l2_requestbuffers requestBuffers = { 0 };
		requestBuffers.count = BUFFER_COUNT;
		requestBuffers.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
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
			fprintf(stderr, "CreateOutputBuffers: requestBuffers.count=%d\n", requestBuffers.count);
		}


		// Map buffers
		//BufferMapping bufferMappings[requestBuffers.count] = { 0 };
		for (int i = 0; i < requestBuffers.count; ++i)
		{
			v4l2_plane planes[VIDEO_MAX_PLANES];

			v4l2_buffer buffer = { 0 };
			buffer.type = requestBuffers.type;
			buffer.memory = V4L2_MEMORY_MMAP;
			buffer.index = i;
			buffer.m.planes = planes;
			buffer.length = 1;

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

			//mapping.Length1 = buffer.m.planes[1].length;
			//mapping.Start1 = mmap(NULL, mapping.Length1,
			//	PROT_READ | PROT_WRITE, /* recommended */
			//	MAP_SHARED,             /* recommended */
			//	mfc_fd,
			//	buffer.m.planes[1].m.mem_offset);
			//if (mapping.Start1 == MAP_FAILED)
			//{
			//	throw Exception("mmap 1 failed.");
			//}

			outputBuffers.push_back(mapping);

			// Queue buffers
			int ret = ioctl(mfc_fd, VIDIOC_QBUF, &buffer);
			if (ret != 0)
			{
				throw Exception("VIDIOC_QBUF failed.");
			}
		}
	}

	void EnumFormats(uint32_t v4l2BufType)
	{
		int io;

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

	M2M(int width, int height, int fps, int bitrate, int gop)
		: width(width), height(height), fps(fps), bitrate(bitrate), gop(gop)
	{
		// O_NONBLOCK prevents deque operations from blocking if no buffers are ready
		mfc_fd = open(decoderName, O_RDWR | O_NONBLOCK, 0);
		if (mfc_fd < 0)
		{
			throw Exception("Failed to open MFC");
		}


		// Check device capabilities
		v4l2_capability cap = { 0 };

		int ret = ioctl(mfc_fd, VIDIOC_QUERYCAP, &cap);
		if (ret != 0)
		{
			throw Exception("VIDIOC_QUERYCAP failed.");
		}

		if ((cap.capabilities & V4L2_CAP_VIDEO_M2M_MPLANE) == 0 ||
			(cap.capabilities & V4L2_CAP_STREAMING) == 0)
		{
			printf("V4L2_CAP_VIDEO_M2M_MPLANE=%d\n", (cap.capabilities & V4L2_CAP_VIDEO_M2M_MPLANE) != 0);
			printf("V4L2_CAP_STREAMING=%d\n", (cap.capabilities & V4L2_CAP_STREAMING) != 0);

			throw Exception("Insufficient capabilities of MFC device.");
		}


		fprintf(stderr, "V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE formats:\n");
		EnumFormats(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
		
		fprintf(stderr, "-------------------------------------------\n");

		fprintf(stderr, "V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE formats:\n");
		EnumFormats(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);


#if 0	// only V4L2_CID_MIN_BUFFERS_FOR_OUTPUT is supported for MFC

		// Get the number of buffers required
		v4l2_control ctrl = { 0 };
		ctrl.id = V4L2_CID_MIN_BUFFERS_FOR_OUTPUT;

		ret = ioctl(mfc_fd, VIDIOC_G_CTRL, &ctrl);
		if (ret != 0)
		{
			fprintf(stderr, "V4L2_CID_MIN_BUFFERS_FOR_OUTPUT VIDIOC_G_CTRL failed.\n");
		}
		else
		{
			fprintf(stderr, "V4L2_CID_MIN_BUFFERS_FOR_OUTPUT=%d\n", ctrl.value);
		}

		ctrl = { 0 };
		ctrl.id = V4L2_CID_MIN_BUFFERS_FOR_CAPTURE;

		ret = ioctl(mfc_fd, VIDIOC_G_CTRL, &ctrl);
		if (ret != 0)
		{
			fprintf(stderr, "V4L2_CID_MIN_BUFFERS_FOR_CAPTURE VIDIOC_G_CTRL failed.\n");
		}
		else
		{
			fprintf(stderr, "V4L2_CID_MIN_BUFFERS_FOR_CAPTURE=%d\n", ctrl.value);
		}

#endif

		ApplyInputSettings();
		CreateInputBuffers();

		ApplyOutputSettings();
		CreateOutputBuffers();

		// Start output stream
		int val = (int)V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

		ret = ioctl(mfc_fd, VIDIOC_STREAMON, &val);
		if (ret != 0)
		{
			throw Exception("VIDIOC_STREAMON failed.");
		}
	}

	~M2M()
	{
		// Stop output stream
		int val = (int)V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

		int ret = ioctl(mfc_fd, VIDIOC_STREAMOFF, &val);
		if (ret != 0)
		{
			throw Exception("VIDIOC_STREAMOFF failed.");
		}

		// Stop input stream
		val = (int)V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

		ret = ioctl(mfc_fd, VIDIOC_STREAMOFF, &val);
		if (ret != 0)
		{
			throw Exception("VIDIOC_STREAMOFF failed.");
		}


		close(mfc_fd);
	}


	bool EncodeNV12(const unsigned char* y, const unsigned char* uv)
	{
		bool result;

		// Reclaim any free input buffers
		v4l2_plane planes[VIDEO_MAX_PLANES];

		v4l2_buffer dqbuf = { 0 };
		dqbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		dqbuf.memory = V4L2_MEMORY_MMAP;
		dqbuf.m.planes = planes;
		dqbuf.length = 2;

		int ret = ioctl(mfc_fd, VIDIOC_DQBUF, &dqbuf);
		if (ret != 0)
		{
			printf("Waiting on V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE buffer.\n");
		}
		else
		{
			freeInputBuffers.push(dqbuf.index);
		}
		

		// Encode
		if (freeInputBuffers.empty())
		{
			printf("no free buffers.\n");
			result = false;
		}
		else
		{
			result = true;
			
			int index = freeInputBuffers.front();
			freeInputBuffers.pop();

			v4l2_plane planes[VIDEO_MAX_PLANES];

			v4l2_buffer buffer = { 0 };
			buffer.type = dqbuf.type;
			buffer.memory = V4L2_MEMORY_MMAP;
			buffer.index = index;
			buffer.m.planes = planes;
			buffer.length = 2;

			int io = ioctl(mfc_fd, VIDIOC_QUERYBUF, &buffer);
			if (io < 0)
			{
				throw Exception("VIDIOC_QUERYBUF failed.");
			}

			// TODO validate buffer size

			// Copy data
			BufferMapping mapping = inputBuffers[buffer.index];
			
			memcpy(mapping.Start0, y, width * height);
			buffer.m.planes[0].bytesused = width * height;

			memcpy(mapping.Start1, uv, width * height / 2);
			buffer.m.planes[1].bytesused = width * height / 2;
			

			// Re-queue buffer
			ret = ioctl(mfc_fd, VIDIOC_QBUF, &buffer);
			if (ret != 0)
			{
				throw Exception("VIDIOC_QBUF failed.");
			}


			if (!streamActive)
			{
				// Start input stream
				int val = (int)V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

				ret = ioctl(mfc_fd, VIDIOC_STREAMON, &val);
				if (ret != 0)
				{
					throw Exception("VIDIOC_STREAMON failed.");
				}
				streamActive = true;
			}
		}

		return result;
	}


	int GetEncodedData(void* dataOut)
	{
		int result;


		// deque
		v4l2_plane planes[VIDEO_MAX_PLANES];

		v4l2_buffer buffer = { 0 };
		buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		buffer.memory = V4L2_MEMORY_MMAP;
		buffer.m.planes = planes;
		buffer.length = 1;

		int ret = ioctl(mfc_fd, VIDIOC_DQBUF, &buffer);
		if (ret != 0)
		{
			printf("Waiting on V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE buffer.\n");
			result = 0;
		}
		else
		{
			result = buffer.m.planes[0].bytesused;

			// Copy data
			BufferMapping mapping = outputBuffers[buffer.index];

			memcpy(dataOut, mapping.Start0, buffer.m.planes[0].bytesused);


			// Re-queue buffer
			ret = ioctl(mfc_fd, VIDIOC_QBUF, &buffer);
			if (ret != 0)
			{
				throw Exception("VIDIOC_QBUF failed.");
			}
		}

		return result;
	}


};
