#include "storage.h"

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define STORAGE_ROOT "netdisk_data"

static bool is_safe_component(const char *component)
{
    return component != NULL && component[0] != '\0' &&
           strcmp(component, ".") != 0 && strcmp(component, "..") != 0 &&
           strchr(component, '/') == NULL;
}

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

static int append_path_segment(char *destination, size_t destination_size,
                               size_t *used, const char *segment)
{
    if (*used > 0 && destination[*used - 1] != '/' &&
        append_path_text(destination, destination_size, used, "/") != 0) {
        return -1;
    }
    return append_path_text(destination, destination_size, used, segment);
}

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
