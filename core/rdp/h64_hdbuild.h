// Harissa64 V2 - builds Harissa64 HD files (h64_hdpack.h) from community
// hi-res texture packs: PNG pictures named the GLideN64 / Rice way
// (NAME#TEXCRC#FMT#SIZE[#PALCRC]_suffix.png).
//
// Shared by the PC converter (platform/host/h64hdconv.cpp) and the console,
// which converts a pack it downloaded. The pictures go to the file as they
// are compressed (a converted pack can be bigger than the console's memory);
// only the index stays in memory.
//
// PNG decoding: stb_image; DXT compression: stb_dxt (third_party/stb, public domain / MIT).
#ifndef H64_HDBUILD_H
#define H64_HDBUILD_H

#include "h64_hdpack.h"

struct H64HdItem
{
    H64HdEntry e;
    u8 *data;
    u32 bytes;
};

int h64_hdbuild_parse(const char *fileName, const char *romName, H64HdEntry *e);
int h64_hdbuild_skip(const char *path);
int h64_hdbuild_make(const u8 *png, u32 len, int maxSide, H64HdItem *item);

struct H64HdBuilder;
H64HdBuilder *h64_hdbuild_begin(const char *path, u32 capacity);
int h64_hdbuild_add(H64HdBuilder *b, H64HdItem *item);
int h64_hdbuild_end(H64HdBuilder *b, u32 *count, u32 *repeated, u32 *bytes);
void h64_hdbuild_abort(H64HdBuilder *b);

#endif
