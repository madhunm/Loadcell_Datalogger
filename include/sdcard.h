#pragma once

#include <Arduino.h>
#include "FS.h"

// Forward declaration of FS type from the fs namespace
namespace fs
{
    class FS;
}

// SD card initialisation and access helpers
bool sdCardInit();
bool sdCardIsMounted();
fs::FS &sdCardGetFs();
