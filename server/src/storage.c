/*
 * 服务器端文件存储辅助模块。
 *
 * 本模块把客户端传来的“用户名 + 逻辑目录 + 文件名”转换为
 * netdisk_data/<username>/... 下的物理路径，并集中完成目录创建和递归
 * 删除。路径校验是安全边界：任何来自网络的路径都应先经过这里处理，
 * 避免利用绝对路径或 ".." 访问其他用户及服务器上的任意文件。
 */
#include "storage.h"

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* 所有用户文件的物理根目录，路径相对于服务器进程的工作目录。 */
#define STORAGE_ROOT "netdisk_data"

/*
 * 判断一个路径分量能否安全地作为用户名或文件名。
 * 分量不能为空、不能是当前/父目录标记，也不能再包含目录分隔符。
 */
static bool is_safe_component(const char *component)
{
    return component != NULL && component[0] != '\0' &&
           strcmp(component, ".") != 0 && strcmp(component, "..") != 0 &&
           strchr(component, '/') == NULL;
}

/*
 * 向已有路径缓冲区末尾追加原始文本，并始终保留结尾的 '\0'。
 * used 同时作为当前长度输入和追加后的长度输出。
 */
static int append_path_text(char *destination, size_t destination_size,
                            size_t *used, const char *text)
{
    size_t length = strlen(text);

    if (*used >= destination_size ||
        length > destination_size - 1 - *used) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(destination + *used, text, length);
    *used += length;
    destination[*used] = '\0';
    return 0;
}

/* 在需要时补充一个斜杠，再追加一个路径段。 */
static int append_path_segment(char *destination, size_t destination_size,
                               size_t *used, const char *segment)
{
    if (*used > 0 && destination[*used - 1] != '/' &&
        append_path_text(destination, destination_size, used, "/") != 0) {
        return -1;
    }
    return append_path_text(destination, destination_size, used, segment);
}

/*
 * 把客户端逻辑路径规范化成不带开头斜杠的相对路径。
 * 重复斜杠和 "." 会被忽略；出现 ".." 时直接拒绝，而不是尝试回退，
 * 从而保证后续拼接结果不会逃出用户存储根目录。
 */
static int normalize_relative_path(const char *input, char *output,
                                   size_t output_size)
{
    const char *cursor = input != NULL ? input : "";
    size_t used = 0;

    if (output_size == 0) {
        errno = EINVAL;
        return -1;
    }
    output[0] = '\0';

    while (*cursor != '\0') {
        const char *end;
        size_t component_length;

        while (*cursor == '/') {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }

        end = strchr(cursor, '/');
        component_length = end != NULL ? (size_t)(end - cursor) : strlen(cursor);
        if (component_length == 1 && cursor[0] == '.') {
            cursor += component_length;
            continue;
        }
        if (component_length == 2 && cursor[0] == '.' && cursor[1] == '.') {
            errno = EPERM;
            return -1;
        }

        if (used > 0) {
            if (used >= output_size - 1) {
                errno = ENAMETOOLONG;
                return -1;
            }
            output[used++] = '/';
        }
        if (component_length > output_size - 1 - used) {
            errno = ENAMETOOLONG;
            return -1;
        }
        memcpy(output + used, cursor, component_length);
        used += component_length;
        output[used] = '\0';
        cursor += component_length;
    }
    return 0;
}

/*
 * 构造用户空间内的完整物理路径。
 * 最终格式为 STORAGE_ROOT/username[/path][/leaf]，所有追加操作都有容量检查。
 */
int storage_build_user_path(char *destination, size_t destination_size,
                            const char *username, const char *path,
                            const char *leaf)
{
    char normalized_path[STORAGE_PATH_SIZE];
    size_t used = 0;

    if (destination == NULL || destination_size == 0) {
        errno = EINVAL;
        return -1;
    }
    if (!is_safe_component(username) ||
        (leaf != NULL && !is_safe_component(leaf)) ||
        normalize_relative_path(path, normalized_path,
                                sizeof(normalized_path)) != 0) {
        errno = EPERM;
        return -1;
    }

    destination[0] = '\0';
    if (append_path_text(destination, destination_size, &used,
                         STORAGE_ROOT) != 0 ||
        append_path_segment(destination, destination_size, &used,
                            username) != 0 ||
        (normalized_path[0] != '\0' &&
         append_path_segment(destination, destination_size, &used,
                             normalized_path) != 0) ||
        (leaf != NULL &&
         append_path_segment(destination, destination_size, &used,
                             leaf) != 0)) {
        return -1;
    }
    return 0;
}

/*
 * 拼接服务器已经信任的父路径和一个子项名称。
 * 该函数负责容量检查，但不会替代 storage_build_user_path() 的安全校验。
 */
int storage_join_paths(char *destination, size_t destination_size,
                       const char *parent, const char *child)
{
    size_t used = 0;

    if (destination == NULL || destination_size == 0 || parent == NULL ||
        child == NULL) {
        errno = EINVAL;
        return -1;
    }
    destination[0] = '\0';
    return append_path_text(destination, destination_size, &used, parent) == 0 &&
           append_path_segment(destination, destination_size, &used, child) == 0
               ? 0
               : -1;
}

/* 创建单级目录；目录已经存在且确实是目录时保持幂等并返回成功。 */
static int ensure_directory(const char *path)
{
    struct stat status;

    if (mkdir(path, 0755) == 0) {
        return 0;
    }
    if (errno == EEXIST && lstat(path, &status) == 0 &&
        S_ISDIR(status.st_mode)) {
        return 0;
    }
    return -1;
}

/* 先创建公共存储根目录，再创建指定用户的根目录。 */
int storage_ensure_user_dir(const char *username)
{
    char user_dir[STORAGE_PATH_SIZE];

    if (storage_build_user_path(user_dir, sizeof(user_dir), username, NULL,
                                NULL) != 0 ||
        ensure_directory(STORAGE_ROOT) != 0 ||
        ensure_directory(user_dir) != 0) {
        return -1;
    }
    return 0;
}

/*
 * 逐个临时截断斜杠来创建中间目录，最后创建完整路径。
 * temporary 是输入路径的可写副本，因此不会修改调用者传入的字符串。
 */
int storage_mkdirs(const char *path)
{
    char temporary[STORAGE_PATH_SIZE];
    char *cursor;
    size_t length;

    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }
    length = strlen(path);
    if (length == 0 || length >= sizeof(temporary)) {
        errno = length == 0 ? EINVAL : ENAMETOOLONG;
        return -1;
    }
    memcpy(temporary, path, length + 1);
    if (length > 1 && temporary[length - 1] == '/') {
        temporary[length - 1] = '\0';
    }

    for (cursor = temporary + 1; *cursor != '\0'; cursor++) {
        if (*cursor != '/') {
            continue;
        }
        *cursor = '\0';
        if (ensure_directory(temporary) != 0) {
            return -1;
        }
        *cursor = '/';
    }
    return ensure_directory(temporary);
}

/*
 * 深度优先删除路径：普通文件直接 unlink，目录则先递归删除子项再 rmdir。
 * 遍历失败时保存 errno，确保 closedir() 不会覆盖真正的失败原因。
 */
int storage_remove_tree(const char *path)
{
    struct stat status;
    DIR *directory;
    struct dirent *entry;
    int result = 0;
    int saved_errno;

    if (lstat(path, &status) != 0) {
        return -1;
    }
    if (!S_ISDIR(status.st_mode)) {
        return unlink(path);
    }

    directory = opendir(path);
    if (directory == NULL) {
        return -1;
    }
    while ((entry = readdir(directory)) != NULL) {
        char child_path[STORAGE_PATH_SIZE];

        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (storage_join_paths(child_path, sizeof(child_path), path,
                               entry->d_name) != 0 ||
            storage_remove_tree(child_path) != 0) {
            result = -1;
            break;
        }
    }
    saved_errno = errno;
    closedir(directory);
    if (result != 0) {
        errno = saved_errno;
        return -1;
    }
    return rmdir(path);
}
