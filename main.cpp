#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <getopt.h>

#include <exception>
#include <vector>
#include <cstring>

#include <linux/videodev2.h> // V4L
#include <sys/mman.h>	// mmap

#include "M2M.h"


// https://upload.wikimedia.org/wikipedia/commons/thumb/4/4e/Cachoeira_Santa_B%C3%A1rbara_-_Rafael_Defavari.jpg/1280px-Cachoeira_Santa_B%C3%A1rbara_-_Rafael_Defavari.jpg
// ffmpeg -i 1280px-Cachoeira_Santa_Bárbara_-_Rafael_Defavari.jpg -f rawvideo -pix_fmt nv12 default.nv12


int main(int argc, char** argv)
{
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
}


