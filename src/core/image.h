/*
 * image.h - image decoding wrapper over stb_image (see image.c).
 *
 * Decodes a PNG/JPEG/BMP/GIF/TGA/PSD file or an in-memory image blob
 * into a malloc'd RGBA8 buffer (4 bytes/pixel, R,G,B,A, row-major,
 * top-down). The caller releases it with image_free() (plain free() -
 * do NOT override STBI_MALLOC/REALLOC/FREE, image_free assumes the
 * default allocator).
 *
 * All functions are C89. path_utf8 follows the same convention as the
 * rest of the core layer: the bytes are handed to fopen as-is (narrow,
 * system codepage); a UTF-8 path with non-ASCII characters only opens
 * when that matches the ANSI codepage, same limitation as config_load
 * and history.c.
 */
#ifndef LLMCHAT32_CORE_IMAGE_H
#define LLMCHAT32_CORE_IMAGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Decode the image file at path_utf8. On success returns 0 and stores
 * the dimensions in *w and *h and a malloc'd RGBA8 buffer in *rgba
 * (caller frees with image_free). On any failure returns non-zero,
 * *rgba is NULL, and *w and *h are left untouched. */
int image_decode_file(const char *path_utf8, int *w, int *h,
                      unsigned char **rgba);

/* Same as image_decode_file but reads from the len bytes at data
 * (memory is not retained; the decode copies what it needs). */
int image_decode_mem(const void *data, int len, int *w, int *h,
                     unsigned char **rgba);

/* Release a buffer returned by image_decode_file/mem. NULL is fine. */
void image_free(unsigned char *rgba);

#ifdef __cplusplus
}
#endif

#endif /* LLMCHAT32_CORE_IMAGE_H */
