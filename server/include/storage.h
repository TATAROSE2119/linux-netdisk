#ifndef NETDISK_STORAGE_H
#define NETDISK_STORAGE_H

#include <stddef.h>

/* 服务器内部拼接出的物理路径所允许的最大缓冲区大小。 */
#define STORAGE_PATH_SIZE 2048

/*
 * 在 netdisk_data/<username>/ 下构造受约束的用户文件路径。
 * path 可以为 NULL、空串或带前导斜杠的逻辑路径；leaf 为可选文件名。
 * 函数会拒绝非法用户名、非法叶子节点以及包含 ".." 的越界路径。
 * 成功返回 0；失败返回 -1，并设置 errno（如 EINVAL、EPERM、ENAMETOOLONG）。
 */
int storage_build_user_path(char *destination, size_t destination_size,
                            const char *username, const char *path,
                            const char *leaf);

/*
 * 将 parent 与单个 child 路径段安全拼接到 destination。
 * 调用者负责提供足够大的输出缓冲区；成功返回 0，失败返回 -1。
 */
int storage_join_paths(char *destination, size_t destination_size,
                       const char *parent, const char *child);

/* 确保存储根目录及指定用户目录存在；已存在的目录也视为成功。 */
int storage_ensure_user_dir(const char *username);

/* 递归创建 path 中的各级目录，语义类似命令行的 mkdir -p。 */
int storage_mkdirs(const char *path);

/*
 * 递归删除文件或目录树。调用前必须先用 storage_build_user_path()
 * 将客户端路径限制在用户根目录内，不能把未经校验的输入直接传入。
 */
int storage_remove_tree(const char *path);

#endif /* NETDISK_STORAGE_H */
