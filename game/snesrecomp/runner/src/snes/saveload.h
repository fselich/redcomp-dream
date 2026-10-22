#pragma once

#include <stddef.h>
#include <stdbool.h>

typedef struct SaveLoadInfo SaveLoadInfo;
typedef void SaveLoadInfoFunc(SaveLoadInfo *info, void *data, size_t data_size);
struct SaveLoadInfo {
  SaveLoadInfoFunc *func;
  /* Optional non-consuming lookahead, relative to the current load position.
   * NULL for saves and streams without lookahead. Never sets a read error. */
  bool (*peek)(SaveLoadInfo *info, size_t offset, void *data, size_t size);
};

//#define SL(x) sli->func(sli, &x, sizeof(x))
