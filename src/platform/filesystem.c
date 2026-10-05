#include "internal.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
/* psapi requires Windows types declared first. */
/* clang-format off */
#include <windows.h>
#include <psapi.h>
/* clang-format on */
#else
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define C_READ_LIMIT (128u * 1024u * 1024u)

uint64_t c_process_peak_memory_bytes(void) {
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS counters;
  if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
    return 0;
  return (uint64_t)counters.PeakWorkingSetSize;
#else
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage))
    return 0;
  return (uint64_t)usage.ru_maxrss * 1024u;
#endif
}
c_status c_new_directory(const char *path) {
  if (!path || !*path)
    return C_INVALID;
#ifdef _WIN32
  if (CreateDirectoryA(path, NULL))
    return C_OK;
  return GetLastError() == ERROR_ALREADY_EXISTS ? C_INVALID : C_IO;
#else
  if (mkdir(path, 0700) == 0)
    return C_OK;
  return errno == EEXIST ? C_INVALID : C_IO;
#endif
}

uint64_t c_monotonic_ms(void) {
#ifdef _WIN32
  return GetTickCount64();
#else
  struct timespec value;
  if (clock_gettime(CLOCK_MONOTONIC, &value))
    return 0;
  return (uint64_t)value.tv_sec * 1000u + (uint64_t)value.tv_nsec / 1000000u;
#endif
}
uint64_t c_monotonic_ns(void) {
#ifdef _WIN32
  LARGE_INTEGER ticks, frequency;
  if (!QueryPerformanceFrequency(&frequency) ||
      !QueryPerformanceCounter(&ticks) || frequency.QuadPart <= 0)
    return 0;
  uint64_t value = (uint64_t)ticks.QuadPart;
  uint64_t rate = (uint64_t)frequency.QuadPart;
  return (value / rate) * UINT64_C(1000000000) +
         (value % rate) * UINT64_C(1000000000) / rate;
#else
  struct timespec value;
  if (clock_gettime(CLOCK_MONOTONIC, &value))
    return 0;
  return (uint64_t)value.tv_sec * UINT64_C(1000000000) +
         (uint64_t)value.tv_nsec;
#endif
}
c_status c_read_file(const char *path, unsigned char **bytes, size_t *length) {
  unsigned char *data = NULL;
  size_t size = 0, capacity = 4096;
  c_status s = C_OK;
  FILE *file;
  if (!path || !*path || !bytes || !length)
    return C_INVALID;
#ifdef _WIN32
  {
    DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes &
         (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
      return C_IO;
  }
#else
  {
    struct stat info;
    if (lstat(path, &info) || !S_ISREG(info.st_mode))
      return C_IO;
  }
#endif
  file = fopen(path, "rb");
  if (!file)
    return C_IO;
  data = (unsigned char *)malloc(capacity);
  if (!data) {
    fclose(file);
    return C_NOMEM;
  }
  for (;;) {
    size_t amount;
    if (size == capacity) {
      unsigned char *grown;
      if (capacity == C_READ_LIMIT) {
        int extra = fgetc(file);
        if (extra != EOF)
          s = C_LIMIT;
        else if (ferror(file))
          s = C_IO;
        break;
      }
      capacity *= 2;
      grown = (unsigned char *)realloc(data, capacity);
      if (!grown) {
        s = C_NOMEM;
        break;
      }
      data = grown;
    }
    amount = fread(data + size, 1, capacity - size, file);
    size += amount;
    if (feof(file))
      break;
    if (ferror(file) || !amount) {
      s = C_IO;
      break;
    }
  }
  if (fclose(file) && s == C_OK)
    s = C_IO;
  if (s != C_OK) {
    free(data);
    return s;
  }
  *bytes = data;
  *length = size;
  return C_OK;
}
c_status c_make_directory(const char *path) {
  if (!path || !*path)
    return C_INVALID;
#ifdef _WIN32
  if (CreateDirectoryA(path, NULL))
    return C_OK;
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    DWORD a = GetFileAttributesA(path);
    return (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) &&
            !(a & FILE_ATTRIBUTE_REPARSE_POINT))
               ? C_OK
               : C_IO;
  }
#else
  if (!mkdir(path, 0700))
    return C_OK;
  if (errno == EEXIST) {
    struct stat info;
    if (!lstat(path, &info) && S_ISDIR(info.st_mode))
      return C_OK;
  }
#endif
  return C_IO;
}
c_status c_write_atomic(const char *path, const void *bytes, size_t length) {
  static unsigned sequence = 0;
  char temporary[4096];
  c_status s = C_OK;
  int count;
  if (!path || !*path || (!bytes && length) || strlen(path) > 4000)
    return C_INVALID;
#ifdef _WIN32
  HANDLE handle;
  DWORD attributes = GetFileAttributesA(path);
  if (attributes != INVALID_FILE_ATTRIBUTES &&
      (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
    return C_IO;
  count = snprintf(temporary, sizeof(temporary), "%s.tmp.%lu.%u", path,
                   (unsigned long)GetCurrentProcessId(), ++sequence);
  if (count < 0 || (size_t)count >= sizeof(temporary))
    return C_LIMIT;
  handle = CreateFileA(temporary, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                       FILE_ATTRIBUTE_NORMAL, NULL);
  if (handle == INVALID_HANDLE_VALUE)
    return C_IO;
  {
    size_t offset = 0;
    while (offset < length) {
      DWORD written = 0,
            chunk = (DWORD)((length - offset) > 1048576 ? 1048576
                                                        : length - offset);
      if (!WriteFile(handle, (const unsigned char *)bytes + offset, chunk,
                     &written, NULL) ||
          !written) {
        s = C_IO;
        break;
      }
      offset += written;
    }
  }
  if (s == C_OK && !FlushFileBuffers(handle))
    s = C_IO;
  if (!CloseHandle(handle))
    s = C_IO;
  if (s == C_OK &&
      !MoveFileExA(temporary, path,
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    s = C_IO;
  if (s != C_OK)
    DeleteFileA(temporary);
#else
  int fd;
  struct stat info;
  if (!lstat(path, &info) && !S_ISREG(info.st_mode))
    return C_IO;
  count = snprintf(temporary, sizeof(temporary), "%s.tmp.%ld.%u", path,
                   (long)getpid(), ++sequence);
  if (count < 0 || (size_t)count >= sizeof(temporary))
    return C_LIMIT;
  fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (fd < 0)
    return C_IO;
  {
    size_t offset = 0;
    while (offset < length) {
      ssize_t written =
          write(fd, (const unsigned char *)bytes + offset, length - offset);
      if (written < 0 && errno == EINTR)
        continue;
      if (written <= 0) {
        s = C_IO;
        break;
      }
      offset += (size_t)written;
    }
  }
  if (s == C_OK && fsync(fd))
    s = C_IO;
  if (close(fd))
    s = C_IO;
  if (s == C_OK && rename(temporary, path))
    s = C_IO;
  if (s != C_OK)
    unlink(temporary);
#endif
  return s;
}
