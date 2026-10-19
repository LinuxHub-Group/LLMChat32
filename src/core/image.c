/*
 * image.c - stb_image wrapper (see image.h).
 *
 * STB_IMAGE_IMPLEMENTATION lives here and only here; the rest of the
 * project just calls image_decode_*() and image_free(). stb_image is
 * compiled in a reduced configuration - STBI_NO_HDR/PIC/PNM drops the
 * float/pow layer and formats we never feed it, STBI_NO_SIMD keeps the
 * scalar decoders only (no emmintrin.h, Win98-safe) - PNG/JPEG/BMP/
 * GIF/TGA/PSD still cover every input Task 16 needs. Allocator
 * overrides are not used, so image_free() is plain free().
 *
 * Open Watcom note: with the defines above, stb_image v2.30 builds
 * warning-free under our -wx, so no warning suppressions are needed
 * here (unlike third_party\wolfssl, which needs its own flag set).
 */
#include "image.h"

#include <stdio.h>
#include <stdlib.h>

#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_SIMD   /* scalar fallbacks only: keeps OW away from emmintrin.h and stays Win98-safe */
#define STB_IMAGE_IMPLEMENTATION
#include "../../third_party/stb_image.h"

int image_decode_mem(const void *data, int len, int *w, int *h,
                     unsigned char **rgba)
{
    unsigned char *p;
    int x, y, n;

    if (rgba == NULL || w == NULL || h == NULL
        || data == NULL || len <= 0)
        return -1;
    *rgba = NULL;
    p = stbi_load_from_memory((const stbi_uc *)data, len, &x, &y, &n, 4);
    if (p == NULL)
        return -1;
    *w = x;
    *h = y;
    *rgba = p;
    return 0;
}

int image_decode_file(const char *path_utf8, int *w, int *h,
                      unsigned char **rgba)
{
    FILE *fp;
    unsigned char *p;
    int x, y, n;

    if (rgba == NULL || w == NULL || h == NULL
        || path_utf8 == NULL || path_utf8[0] == '\0')
        return -1;
    *rgba = NULL;
    fp = fopen(path_utf8, "rb");
    if (fp == NULL)
        return -1;
    p = stbi_load_from_file(fp, &x, &y, &n, 4);
    fclose(fp);
    if (p == NULL)
        return -1;
    *w = x;
    *h = y;
    *rgba = p;
    return 0;
}

void image_free(unsigned char *rgba)
{
    free(rgba);
}
