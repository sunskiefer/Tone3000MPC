/* THING4000's chain picture strip: 8 squares on a hardware overlay plane of the Force's display (see chainpics.c). */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define CP_N 8
#define CP_MAXPIC 256       /* pictures are shrunk to at most this many pixels on the longest side when decoded */

struct cp_inst;
/* one per plugin instance; run_overlay=1 lets the first instance start the strip's thread (data_dir/chain/ holds its log and layout) */
struct cp_inst *cp_open(const char *data_dir, int run_overlay);
void cp_close(struct cp_inst *c);
void cp_touch(struct cp_inst *c);
void cp_set_page(struct cp_inst *c, int pictures_page_open);   /* the grid is drawn only while this instance's PICTURES page is open */                                                  /* this instance's page is the one being used */
void cp_set_state(struct cp_inst *c, const int *used, const int *bypass, int sel);  /* 8 flags each */
void cp_set_picture(struct cp_inst *c, int blk, const uint8_t *rgba, int w, int h); /* straight RGBA; NULL clears; copied */
/* png / jpg file -> straight RGBA, shrunk so the longest side is <= CP_MAXPIC; NULL if unreadable. Free with cp_free. */
uint8_t *cp_decode(const char *path, int *w, int *h);
void cp_free(void *p);
/* offline: the strip drawn for this instance (premultiplied BGRA bytes, landscape), 834 x 70; 0 on success */
int cp_render(struct cp_inst *c, uint8_t *out, int cap, int *w, int *h);

void cp_overlay_start(const char *data_dir);
void cp_overlay_stop(void);
#ifdef __cplusplus
}
#endif
