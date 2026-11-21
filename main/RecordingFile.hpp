#pragma once

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <strings.h>
#include <unistd.h>

namespace CTAG::AUDIO {

// Scan the whole directory, independent of the panel's bounded browser list.
inline bool NextRecordingName(const char *directory, char *name, size_t size) {
    if (!name || size < sizeof("tape_000.wav")) return false;
    name[0] = '\0';
    DIR *dir = opendir(directory);
    if (!dir) return false;
    unsigned char taken[125] = {};
    while (dirent *entry = readdir(dir)) {
        const char *s = entry->d_name;
        if (strlen(s) != 12 || strncasecmp(s, "tape_", 5) != 0 ||
            strcasecmp(s + 8, ".wav") != 0) continue;
        if (s[5] < '0' || s[5] > '9' || s[6] < '0' || s[6] > '9' ||
            s[7] < '0' || s[7] > '9') continue;
        int number = (s[5] - '0') * 100 + (s[6] - '0') * 10 + s[7] - '0';
        // Directories also occupy names and must not be selected.
        taken[number / 8] |= 1u << (number % 8);
    }
    closedir(dir);
    for (int number = 1; number <= 999; ++number) {
        if (taken[number / 8] & (1u << (number % 8))) continue;
        snprintf(name, size, "tape_%03d.wav", number);
        return true;
    }
    return false;
}

// The name preview is advisory. Exclusive creation also protects against a
// collision between preview and START, and protects explicit bench paths.
inline FILE *OpenNewRecording(const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0) return nullptr;
    FILE *file = fdopen(fd, "wb");
    if (!file) {
        int error = errno;
        close(fd);
        unlink(path); // only the file this call just created
        errno = error;
    }
    return file;
}

} // namespace CTAG::AUDIO
