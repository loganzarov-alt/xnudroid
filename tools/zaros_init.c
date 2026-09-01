#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>

static void kmsg(const char *s)
{
    int fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    if (fd >= 0) {
        write(fd, s, strlen(s));
        close(fd);
    }
}

static void mkdir_if_needed(const char *p)
{
    if (mkdir(p, 0755) && errno != EEXIST) {}
}

static void setup_fs(void)
{
    mkdir_if_needed("/proc");
    mkdir_if_needed("/sys");
    mkdir_if_needed("/dev");
    mount("proc", "/proc", "proc", 0, NULL);
    mount("sysfs", "/sys", "sysfs", 0, NULL);
    mount("devtmpfs", "/dev", "devtmpfs", 0, "mode=0755");

    mkdir_if_needed("/dev/graphics");
    if (access("/dev/console", F_OK)) mknod("/dev/console", S_IFCHR | 0600, makedev(5, 1));
    if (access("/dev/kmsg", F_OK)) mknod("/dev/kmsg", S_IFCHR | 0600, makedev(1, 11));
    if (access("/dev/fb0", F_OK)) mknod("/dev/fb0", S_IFCHR | 0600, makedev(29, 0));
}

static void set_backlight(void)
{
    const char *paths[] = {
        "/sys/class/backlight/panel0-backlight/brightness",
        "/sys/class/leds/lcd-backlight/brightness",
        NULL
    };
    for (int i = 0; paths[i]; ++i) {
        int fd = open(paths[i], O_WRONLY | O_CLOEXEC);
        if (fd >= 0) {
            const char *v = "1200\n";
            write(fd, v, strlen(v));
            close(fd);
            return;
        }
    }
}

static uint32_t chan(uint8_t c, struct fb_bitfield f)
{
    if (!f.length) return 0;
    uint32_t max = (1u << f.length) - 1u;
    return (((uint32_t)c * max + 127u) / 255u) << f.offset;
}

static uint32_t pixel(uint8_t r, uint8_t g, uint8_t b, const struct fb_var_screeninfo *v)
{
    return chan(r, v->red) | chan(g, v->green) | chan(b, v->blue) |
           (v->transp.length ? chan(255, v->transp) : 0);
}

/* 5x7 glyphs, rows stored in low 5 bits. */
static const uint8_t G_Z[7] = {31,1,2,4,8,16,31};
static const uint8_t G_A[7] = {14,17,17,31,17,17,17};
static const uint8_t G_R[7] = {30,17,17,30,20,18,17};
static const uint8_t G_O[7] = {14,17,17,17,17,17,14};
static const uint8_t G_S[7] = {15,16,16,14,1,1,30};

static void rect32(uint8_t *mem, const struct fb_fix_screeninfo *f,
                   unsigned x, unsigned y, unsigned w, unsigned h,
                   uint32_t px, unsigned bytespp)
{
    for (unsigned yy = y; yy < y + h; ++yy) {
        uint8_t *row = mem + (size_t)yy * f->line_length;
        for (unsigned xx = x; xx < x + w; ++xx)
            memcpy(row + (size_t)xx * bytespp, &px, bytespp);
    }
}

static void glyph(uint8_t *mem, const struct fb_fix_screeninfo *f,
                  const uint8_t g[7], unsigned x, unsigned y, unsigned scale,
                  uint32_t px, unsigned bytespp)
{
    for (unsigned row = 0; row < 7; ++row)
        for (unsigned col = 0; col < 5; ++col)
            if (g[row] & (1u << (4 - col)))
                rect32(mem, f, x + col * scale, y + row * scale,
                       scale, scale, px, bytespp);
}

static int draw_zaros(const char *path)
{
    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) return -1;

    struct fb_var_screeninfo v;
    struct fb_fix_screeninfo f;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &v) || ioctl(fd, FBIOGET_FSCREENINFO, &f)) {
        close(fd);
        return -1;
    }

    if (v.bits_per_pixel != 32 || !f.line_length || !f.smem_len) {
        close(fd);
        return -1;
    }

    uint8_t *mem = mmap(NULL, f.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mem == MAP_FAILED) {
        close(fd);
        return -1;
    }

    ioctl(fd, FBIOBLANK, FB_BLANK_UNBLANK);
    set_backlight();

    unsigned bytespp = v.bits_per_pixel / 8;
    uint32_t bg = pixel(8, 12, 18, &v);
    uint32_t fg = pixel(245, 245, 245, &v);
    rect32(mem, &f, 0, 0, v.xres, v.yres, bg, bytespp);

    const uint8_t *word[5] = { G_Z, G_A, G_R, G_O, G_S };
    unsigned scale = v.xres / 40;
    if (scale < 4) scale = 4;
    if (scale > 32) scale = 32;
    unsigned gw = 5 * scale, gap = scale * 2;
    unsigned total = 5 * gw + 4 * gap;
    unsigned x = v.xres > total ? (v.xres - total) / 2 : 0;
    unsigned y = v.yres > 7 * scale ? (v.yres - 7 * scale) / 2 : 0;
    for (unsigned i = 0; i < 5; ++i) {
        glyph(mem, &f, word[i], x, y, scale, fg, bytespp);
        x += gw + gap;
    }

    msync(mem, f.smem_len, MS_SYNC);
    munmap(mem, f.smem_len);
    close(fd);
    kmsg("ZAROS: framebuffer marker drawn\n");
    return 0;
}

int main(void)
{
    setup_fs();
    kmsg("ZAROS: init started on willow source reference\n");

    for (int i = 0; i < 200; ++i) {
        if (!draw_zaros("/dev/graphics/fb0") || !draw_zaros("/dev/fb0"))
            break;
        usleep(100000);
    }

    for (;;) sleep(3600);
    return 0;
}
