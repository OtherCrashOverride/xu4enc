all:
	gcc -g -std=c99 -c exynos_fimg2d.c -I/usr/include/uapi/drm/
	g++ -g -std=c++11 main.cpp -o xu4enc exynos_fimg2d.o -I/usr/include/uapi/drm/ -ldrm

