/* Application coordinates use a 480-pixel content area; reserve the first
 * 24 physical pixels for system status while retaining the navigation bar. */
static int hosted_content_painting;
static U32 hosted_raw_pointer;
static int hosted_content_y(int y) { return 24 + y * 19 / 20; }
static U32 hosted_content_pointer(U32 lp)
{
    int y = (int)(short)(lp >> 16);
    y = y < 24 ? 0 : (y - 24) * 20 / 19;
    return (lp & 0xffffu) | ((U32)(y & 0xffff) << 16);
}
#define HOSTED_SYSTEM_PAINT(target, dc) do { \
    int saved_hosted_painting = hosted_content_painting; \
    hosted_content_painting = 0; (target)->paint(dc); \
    hosted_content_painting = saved_hosted_painting; \
} while (0)
