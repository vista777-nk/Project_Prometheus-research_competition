/**
 * @file unity.c
 * @brief 极简 Unity 风格测试框架实现 (Host 测试专用，不参与交叉编译)
 *
 * 断言失败通过 longjmp 中止当前用例并继续执行下一个，与官方 Unity 行为一致。
 */
#include "unity.h"

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

static const char *s_file = "unknown";
static const char *s_test = "unknown";
static int s_tests = 0;
static int s_failures = 0;
static int s_current_failed = 0;
static jmp_buf s_abort_frame;

static void report_failure(int line, const char *message, const char *detail)
{
    s_current_failed = 1;
    s_failures++;
    printf("%s:%d:%s:FAIL", s_file, line, s_test);
    if (detail != NULL && detail[0] != '\0') {
        printf(": %s", detail);
    }
    if (message != NULL && message[0] != '\0') {
        printf(" (%s)", message);
    }
    printf("\n");
    fflush(stdout);
    longjmp(s_abort_frame, 1);
}

void UnityBegin(const char *filename)
{
    s_file = (filename != NULL) ? filename : "unknown";
    s_tests = 0;
    s_failures = 0;
    printf("----------------------------------------------------------\n");
    fflush(stdout);
}

void UnitySetTestFile(const char *filename)
{
    if (filename != NULL) {
        s_file = filename;
    }
}

int UnityEnd(void)
{
    printf("----------------------------------------------------------\n");
    printf("%d Tests %d Failures 0 Ignored\n", s_tests, s_failures);
    printf("%s\n", (s_failures == 0) ? "OK" : "FAIL");
    fflush(stdout);
    return s_failures;
}

void UnityDefaultTestRun(void (*func)(void), const char *name, int line)
{
    (void)line;
    s_test = (name != NULL) ? name : "unnamed";
    s_tests++;
    s_current_failed = 0;

    if (setjmp(s_abort_frame) == 0) {
        setUp();
        func();
        tearDown();
    } else {
        /* 断言失败已在 report_failure 中打印，这里仍需收尾 */
        tearDown();
    }

    if (!s_current_failed) {
        printf("%s:%s:PASS\n", s_file, s_test);
        fflush(stdout);
    }
}

void UnityFail(const char *message, int line)
{
    report_failure(line, message, "explicit failure");
}

void UnityAssertTrue(int condition, const char *message, int line, const char *expr)
{
    if (condition) {
        return;
    }
    char detail[192];
    snprintf(detail, sizeof(detail), "expected TRUE: %s", (expr != NULL) ? expr : "?");
    report_failure(line, message, detail);
}

void UnityAssertEqualInt(long expected, long actual, const char *message, int line)
{
    if (expected == actual) {
        return;
    }
    char detail[128];
    snprintf(detail, sizeof(detail), "expected %ld, was %ld", expected, actual);
    report_failure(line, message, detail);
}

void UnityAssertEqualUInt(unsigned long expected, unsigned long actual,
                          const char *message, int line)
{
    if (expected == actual) {
        return;
    }
    char detail[128];
    snprintf(detail, sizeof(detail), "expected %lu, was %lu", expected, actual);
    report_failure(line, message, detail);
}

void UnityAssertEqualHex(unsigned long expected, unsigned long actual, int width,
                         const char *message, int line)
{
    if (expected == actual) {
        return;
    }
    char detail[128];
    snprintf(detail, sizeof(detail), "expected 0x%0*lX, was 0x%0*lX",
             width, expected, width, actual);
    report_failure(line, message, detail);
}

void UnityAssertFloatWithin(double delta, double expected, double actual,
                            const char *message, int line)
{
    double diff = actual - expected;
    if (diff < 0.0) {
        diff = -diff;
    }
    if (delta < 0.0) {
        delta = -delta;
    }
    /* NaN 一律判失败：任何与 NaN 的比较都为假 */
    if (diff <= delta) {
        return;
    }
    char detail[160];
    snprintf(detail, sizeof(detail), "expected %.6f +/- %.6f, was %.6f",
             expected, delta, actual);
    report_failure(line, message, detail);
}

void UnityAssertEqualMemory(const void *expected, const void *actual, size_t len,
                            const char *message, int line)
{
    if (expected == NULL || actual == NULL) {
        report_failure(line, message, "memory compare against NULL");
        return;
    }
    const unsigned char *e = (const unsigned char *)expected;
    const unsigned char *a = (const unsigned char *)actual;
    for (size_t i = 0; i < len; i++) {
        if (e[i] != a[i]) {
            char detail[160];
            snprintf(detail, sizeof(detail),
                     "memory differs at byte %lu: expected 0x%02X, was 0x%02X",
                     (unsigned long)i, e[i], a[i]);
            report_failure(line, message, detail);
            return;
        }
    }
}

void UnityAssertPointer(const void *pointer, int expect_null,
                        const char *message, int line)
{
    const int is_null = (pointer == NULL) ? 1 : 0;
    if (is_null == expect_null) {
        return;
    }
    report_failure(line, message,
                   expect_null ? "expected NULL pointer" : "expected non-NULL pointer");
}
