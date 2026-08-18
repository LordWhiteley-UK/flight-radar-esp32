#ifndef REPLAY_H
#define REPLAY_H

#include <stdbool.h>

/* Load a recorded OpenSky states/all JSON file into memory. Returns true on
   success. The pointer returned by replay_json() stays valid for the process
   lifetime. */
bool replay_load(const char *path);
const char *replay_json(void);

#endif /* REPLAY_H */