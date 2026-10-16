#include "performance_report.h"

#include <math.h>
#include <string.h>

PerformanceReportStatus PerformanceReportEvaluate(const char *renderer, const char *vendor,
                                                  double framesPerSecond)
{
    if ((renderer == NULL) || (vendor == NULL) || (strstr(renderer, "RTX A2000") == NULL) ||
        (strstr(vendor, "NVIDIA") == NULL))
    {
        return PERFORMANCE_REPORT_NOT_TARGET_ADAPTER;
    }
    if (!isfinite(framesPerSecond) || (framesPerSecond < PERFORMANCE_REPORT_TARGET_FPS))
    {
        return PERFORMANCE_REPORT_BELOW_TARGET;
    }
    return PERFORMANCE_REPORT_TARGET_MET;
}

const char *PerformanceReportStatusName(PerformanceReportStatus status)
{
    switch (status)
    {
        case PERFORMANCE_REPORT_NOT_TARGET_ADAPTER:
            return "NOT TARGET ADAPTER";
        case PERFORMANCE_REPORT_BELOW_TARGET:
            return "BELOW 60 FPS";
        case PERFORMANCE_REPORT_TARGET_MET:
            return "MEETS RTX A2000 60 FPS TARGET";
        default:
            return "INVALID STATUS";
    }
}
