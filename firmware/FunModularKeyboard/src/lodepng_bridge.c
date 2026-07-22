#include <stdlib.h>

#ifndef LV_USE_PNG
#define LV_USE_PNG 1
#endif

#define LODEPNG_NO_COMPILE_ALLOCATORS

void* lodepng_malloc(size_t size)
{
	return malloc(size);
}

void* lodepng_realloc(void* ptr, size_t new_size)
{
	return realloc(ptr, new_size);
}

void lodepng_free(void* ptr)
{
	free(ptr);
}

#include "../.pio/libdeps/esp32s3minin4r2/lvgl/src/extra/libs/png/lodepng.c"