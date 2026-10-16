#ifndef PERFORMANCE_REPORT_H
#define PERFORMANCE_REPORT_H

typedef enum PerformanceReportStatus
{
    PERFORMANCE_REPORT_NOT_TARGET_ADAPTER,
    PERFORMANCE_REPORT_BELOW_TARGET,
    PERFORMANCE_REPORT_TARGET_MET
} PerformanceReportStatus;

#define PERFORMANCE_REPORT_TARGET_FPS 60.0

PerformanceReportStatus PerformanceReportEvaluate(const char *renderer, const char *vendor,
                                                  double framesPerSecond);
const char *PerformanceReportStatusName(PerformanceReportStatus status);

#endif
