#ifndef NETDISK_STORAGE_H
#define NETDISK_STORAGE_H

#include <stddef.h>

#define STORAGE_PATH_SIZE 2048

int storage_build_user_path(char *destination, size_t destination_size,
                            const char *username, const char *path,
                            const char *leaf);
int storage_join_paths(char *destination, size_t destination_size,
                       const char *parent, const char *child);
int storage_ensure_user_dir(const char *username);
int storage_mkdirs(const char *path);
int storage_remove_tree(const char *path);

#endif /* NETDISK_STORAGE_H */
