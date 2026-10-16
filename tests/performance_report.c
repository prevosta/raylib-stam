#include "performance_report.h"

#include <math.h>
#include <stdio.h>

static int CheckStatus(const char *renderer, const char *vendor, double framesPerSecond,
                       PerformanceReportStatus expected, const char *description)
{
    const PerformanceReportStatus actual =
        PerformanceReportEvaluate(renderer, vendor, framesPerSecond);
    if (actual != expected)
    {
        fprintf(stderr, "%s: expected %s, received %s\n", description,
                PerformanceReportStatusName(expected), PerformanceReportStatusName(actual));
        return 1;
    }
    return 0;
}

int main(void)
{
    int failures = 0;
    failures += CheckStatus(NULL, "NVIDIA Corporation", 120.0, PERFORMANCE_REPORT_NOT_TARGET_ADAPTER,
                           "Missing renderer");
    failures += CheckStatus("Intel Iris Xe", "Intel", 120.0, PERFORMANCE_REPORT_NOT_TARGET_ADAPTER,
                            "Non-target renderer");
    failures += CheckStatus("NVIDIA RTX A2000 Laptop GPU", NULL, 120.0,
                            PERFORMANCE_REPORT_NOT_TARGET_ADAPTER, "Missing vendor");
    failures += CheckStatus("RTX A2000 compatible adapter", "Intel", 120.0,
                            PERFORMANCE_REPORT_NOT_TARGET_ADAPTER, "Non-NVIDIA vendor");
    failures += CheckStatus("NVIDIA RTX A2000 Laptop GPU", "NVIDIA Corporation", 59.99,
                            PERFORMANCE_REPORT_BELOW_TARGET, "Below target frame rate");
    failures += CheckStatus("NVIDIA RTX A2000 Laptop GPU", "NVIDIA Corporation",
                            PERFORMANCE_REPORT_TARGET_FPS,
                            PERFORMANCE_REPORT_TARGET_MET, "Target frame rate");
    failures += CheckStatus("NVIDIA RTX A2000 Laptop GPU", "NVIDIA Corporation", NAN,
                            PERFORMANCE_REPORT_BELOW_TARGET, "Non-finite frame rate");
    failures += CheckStatus("NVIDIA RTX A2000 Laptop GPU", "NVIDIA Corporation", INFINITY,
                            PERFORMANCE_REPORT_BELOW_TARGET, "Infinite frame rate");

    if (failures != 0)
    {
        return 1;
    }
    puts("Performance acceptance classification tests passed.");
    return 0;
}
