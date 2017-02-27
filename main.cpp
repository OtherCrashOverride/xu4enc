#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <getopt.h>

#include <exception>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <thread>

#include <linux/videodev2.h> // V4L
#include <sys/mman.h>	// mmap

#include "M2M.h"


const int DEFAULT_BITRATE = 1000000 * 5;


struct option longopts[] = {
	{ "width",			required_argument,	NULL,	'w' },
	{ "height",			required_argument,	NULL,	'h' },
	{ "fps",			required_argument,	NULL,	'f' },
	{ "bitrate",		required_argument,	NULL,	'b' },
	{ "gop",			required_argument,	NULL,	'g' },
	{ 0, 0, 0, 0 }
};


int main(int argc, char** argv)
{
	int io;


	// options
	int width = -1;
	int height = -1;
	int framerate = -1;
	int bitrate = DEFAULT_BITRATE;
	int gop = 10;


	int c;
	while ((c = getopt_long(argc, argv, "w:h:f:b:g:", longopts, NULL)) != -1)
	{
		switch (c)
		{
			case 'w':
				width = atoi(optarg);
				break;

			case 'h':
				height = atoi(optarg);
				break;

			case 'f':
				framerate = atoi(optarg);
				break;

			case 'b':
				bitrate = atoi(optarg);
				break;

			case 'g':
				gop = atoi(optarg);
				break;

			default:
				throw Exception("Unknown option.");
		}
	}


	if (width == -1 || height == -1 ||
		framerate == -1)
	{
		throw Exception("Required parameter missing.");
	}



	// Initialize the encoder
	fprintf(stderr, "Initialize encoder: width=%d, height=%d, frame_rate=%d, bit_rate=%d, gop=%d\n",
		width, height, framerate, bitrate, gop);

	M2M codec(width, height, framerate, bitrate, gop);
	

	// Start streaming
	int frameCount = 0;

	// TODO: smart pointer
	int bufferSize = width * height;	// Y	
	bufferSize += bufferSize / 2;	// U, V

	unsigned char* input = new unsigned char[bufferSize];

	const int outputBufferSize = width * height * 4;
	char* outputBuffer = new char[outputBufferSize];

	while (true)
	{
		// get buffer
		ssize_t totalRead = 0;
		while (totalRead < bufferSize)
		{
			ssize_t readCount = read(STDIN_FILENO, input + totalRead, bufferSize - totalRead);
			if (readCount <= 0)
			{
				//throw Exception("read failed.");

				// End of stream?
				fprintf(stderr, "read failed. (%d)\n", readCount);

				// TODO: Signal codec and flush
				break;
			}
			else
			{
				totalRead += readCount;
			}
		}

		if (totalRead < bufferSize)
		{
			fprintf(stderr, "read underflow. (%d of %d)\n", totalRead, bufferSize);
			break;
		}


		// Encode the video frames
		while (!codec.EncodeNV12(&input[0], &input[width * height]))
		{
			fprintf(stderr, "codec.EncodeN12 failed.\n");
			std::this_thread::yield();
		}

		while (true)
		{
			int outCnt = codec.GetEncodedData(&outputBuffer[0]);
			if (outCnt <= 0)
			{
				break;
			}

			size_t offset = 0;
			while (offset < outCnt)
			{
				ssize_t writeCount = write(STDOUT_FILENO, &outputBuffer[0] + offset, outCnt - offset);
				if (writeCount < 0)
				{
					throw Exception("write failed.");
				}
				else
				{
					offset += writeCount;
				}
			}
		}

		// Stats
		if ((frameCount % 100) == 0)
		{
			fprintf(stderr, "frameCount=%d.\n", frameCount);
		}

		++frameCount;
	}

	return 0;

#if 0
	// Load the NV12 test data
	int fd = open("default.nv12", O_RDONLY);
	if (fd < 0)
	{
		printf("open failed.\n");
		throw std::exception();
	}
	
	off_t length = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_SET);
	
	std::vector<char> data(length);
	ssize_t cnt = read(fd, &data[0], length);
	
	close(fd);
	
	if (cnt != length)
	{
		printf("read failed.\n");
		throw std::exception();
	}
	
	// Create an output file
	mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	int fdOut = open("default.h264", O_CREAT | O_TRUNC | O_WRONLY, mode);
	if (fdOut < 0)
	{
		printf("open default.h264 failed\n");
		throw std::exception();
	}

	// Initialize the encoder
	M2M codec(1280, 720, 30);

	// Encode the video frames
	const int BUFFER_SIZE = 1280 * 720 * 4;
	char buffer[BUFFER_SIZE];
	int frameCount = 0;
	while (frameCount < 30 * 30)
	{
		printf("loop #%d\n", frameCount);

		if (!codec.EncodeNV12(&data[0], &data[1280 * 720]))
		{
			printf("codec.EncodeN12 failed.\n");
		}
		else
		{
			frameCount++;
		}

		int outCnt;
		do
		{
			outCnt = codec.GetEncodedData(&buffer[0]);
			int offset = 0;
			while (offset < outCnt)
			{
				size_t cnt = write(fdOut, buffer + offset, outCnt - offset);
				if (cnt > 0)
				{
					offset += cnt;
				}
				else
				{
					break;
				}

				printf("write=%d\n", outCnt);
			}
		} while (outCnt > 0);
	}

	close(fdOut);
	
	return 0;
#endif
}


