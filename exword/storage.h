/* EX-word OS-world FS bridge. No firmware syscall from gint world.
 * Read-only guest files; both native disks are browseable. */
#ifndef NTOS_EXWORD_STORAGE_H
#define NTOS_EXWORD_STORAGE_H
#include <stdint.h>
#define NTOS_EXWORD_LIST_MAX 48u
#define NTOS_EXWORD_NAME_MAX 48u
#define NTOS_EXWORD_PATH_MAX 160u
struct ntos_exword_listing {
    unsigned count;
    int result;
    char names[NTOS_EXWORD_LIST_MAX][NTOS_EXWORD_NAME_MAX];
    uint32_t kinds[NTOS_EXWORD_LIST_MAX];
};
static inline const char *ntos_exword_pattern(unsigned volume)
{
    return volume == 0u ? "\\\\drv0\\*" :
           volume == 1u ? "\\\\crd0\\*" : (const char *)0;
}
#ifdef __cplusplus
extern "C" {
#endif
int ntos_exword_scan(unsigned volume, struct ntos_exword_listing *out);
/* Resolve installed app's SYSROOT with case-accurate directory entries.
 * Returns 0 only when SYSROOT is actually visible on the given volume. */
int ntos_exword_find_sysroot(unsigned volume, char out[NTOS_EXWORD_PATH_MAX]);
int ntos_exword_scan_path(const char *pattern, struct ntos_exword_listing *out);
int ntos_exword_scan_page(const char *pattern,unsigned skip,
                         struct ntos_exword_listing *out,unsigned *more);
int ntos_exword_file_size(const char *path, uint32_t *size);
int ntos_exword_read_at(const char *path, uint32_t offset, void *out, uint32_t count);
/* Read-only WinCE file primitives via OS-world transitions. */
int ntos_exword_open_read(const char *path);
int ntos_exword_read_fd(int fd, void *out, uint32_t count);
int ntos_exword_seek_fd(int fd, int offset, uint32_t method);
int ntos_exword_size_fd(int fd);
int ntos_exword_close_fd(int fd);
#ifdef __cplusplus
}
#endif
#endif
