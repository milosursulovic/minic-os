// Generic image compositing - see image.h. Deliberately knows nothing
// about what any particular image depicts (the cursor, or any future
// icon/sprite) - that's the whole point of pulling this out of
// window.c's old draw_cursor(), which used to hand-branch its own
// bitmap loop directly in compositor code.

#include "image.h"
#include "window.h"

void draw_image(u32 x, u32 y, const image* img) {
    u32 row = 0;
    while (row < img->height) {
        u32 col = 0;
        while (col < img->width) {
            u32 pixel = img->pixels[row * img->width + col];
            if (pixel != IMAGE_TRANSPARENT) {
                bb_put_pixel(x + col, y + row, pixel);
            }
            col = col + 1;
        }
        row = row + 1;
    }
}
