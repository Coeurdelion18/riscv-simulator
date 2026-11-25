#include "hazard.h"
#include <string.h>

static const HazardDetector *const g_detectors[] = {
    &hazard_none,
    &hazard_stall,
    &hazard_forward,
};

#define NUM_DETECTORS ((int)(sizeof(g_detectors) / sizeof(g_detectors[0])))

const HazardDetector *hazard_find(const char *name) {
    for (int i = 0; i < NUM_DETECTORS; i++) {
        if (strcmp(g_detectors[i]->name, name) == 0) return g_detectors[i];
    }
    return NULL;
}

const char *hazard_names(void) {
    return "none, stall, forward";
}
