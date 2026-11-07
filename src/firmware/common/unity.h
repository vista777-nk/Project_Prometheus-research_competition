/**
 * @file unity.h
 * @brief 极简 Unity 风格单元测试框架 (Host 测试专用)
 *
 * 为什么不直接引入 ThrowTheSwitch/Unity 官方源码：
 *   - 本项目 CI 需要零外部下载即可 `make test`，不引入 submodule / vendor 目录
 *   - 固件侧只需要断言 + 用例登记 + 汇总输出这一小撮能力
 * 因此这里实现官方 Unity 的 API 子集，宏名与语义保持一致，
 * 将来若需要完整 Unity (mock / fixture / 参数化)，替换本文件即可，测试代码不用改。
 *
 * 使用约定：
 *   - 每个 test_xxx.c 只导出一个 `void run_xxx_tests(void)`，内部用 RUN_TEST() 登记用例
 *   - 整个测试二进制只有 test_main.c 提供 main()，并定义 setUp() / tearDown()
 *   - 交叉编译固件时**不链接**本文件
 */
#ifndef FIRMWARE_COMMON_UNITY_H
#define FIRMWARE_COMMON_UNITY_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- 由测试工程提供 (test_main.c 中各定义一次) --- */
void setUp(void);
void tearDown(void);

/* --- 框架内部 API，正常情况下只通过下面的宏使用 --- */
void UnityBegin(const char *filename);
int  UnityEnd(void);
void UnityDefaultTestRun(void (*func)(void), const char *name, int line);
void UnitySetTestFile(const char *filename);

void UnityFail(const char *message, int line);
void UnityAssertTrue(int condition, const char *message, int line, const char *expr);
void UnityAssertEqualInt(long expected, long actual, const char *message, int line);
void UnityAssertEqualUInt(unsigned long expected, unsigned long actual,
                          const char *message, int line);
void UnityAssertEqualHex(unsigned long expected, unsigned long actual, int width,
                         const char *message, int line);
void UnityAssertFloatWithin(double delta, double expected, double actual,
                            const char *message, int line);
void UnityAssertEqualMemory(const void *expected, const void *actual, size_t len,
                            const char *message, int line);
void UnityAssertPointer(const void *pointer, int expect_null,
                        const char *message, int line);

/* --- 用例登记与汇总 --- */
#define UNITY_BEGIN()          UnityBegin(__FILE__)
#define UNITY_END()            UnityEnd()
#define RUN_TEST(func)         UnityDefaultTestRun(func, #func, __LINE__)
/** 多文件测试时，在各 run_xxx_tests() 开头调用，让失败信息指向正确的源文件 */
#define UNITY_SET_FILE()       UnitySetTestFile(__FILE__)

/* --- 断言宏 --- */
#define TEST_FAIL_MESSAGE(msg) \
    UnityFail((msg), __LINE__)

#define TEST_ASSERT_TRUE_MESSAGE(cond, msg) \
    UnityAssertTrue((cond) ? 1 : 0, (msg), __LINE__, #cond)
#define TEST_ASSERT_TRUE(cond) \
    TEST_ASSERT_TRUE_MESSAGE((cond), NULL)

#define TEST_ASSERT_FALSE_MESSAGE(cond, msg) \
    UnityAssertTrue((cond) ? 0 : 1, (msg), __LINE__, "!(" #cond ")")
#define TEST_ASSERT_FALSE(cond) \
    TEST_ASSERT_FALSE_MESSAGE((cond), NULL)

#define TEST_ASSERT_EQUAL_INT_MESSAGE(expected, actual, msg) \
    UnityAssertEqualInt((long)(expected), (long)(actual), (msg), __LINE__)
#define TEST_ASSERT_EQUAL_INT(expected, actual) \
    TEST_ASSERT_EQUAL_INT_MESSAGE((expected), (actual), NULL)
#define TEST_ASSERT_EQUAL(expected, actual) \
    TEST_ASSERT_EQUAL_INT((expected), (actual))

#define TEST_ASSERT_EQUAL_UINT_MESSAGE(expected, actual, msg) \
    UnityAssertEqualUInt((unsigned long)(expected), (unsigned long)(actual), (msg), __LINE__)
#define TEST_ASSERT_EQUAL_UINT(expected, actual) \
    TEST_ASSERT_EQUAL_UINT_MESSAGE((expected), (actual), NULL)

#define TEST_ASSERT_EQUAL_HEX8_MESSAGE(expected, actual, msg) \
    UnityAssertEqualHex((unsigned long)(expected) & 0xFFuL, \
                        (unsigned long)(actual) & 0xFFuL, 2, (msg), __LINE__)
#define TEST_ASSERT_EQUAL_HEX8(expected, actual) \
    TEST_ASSERT_EQUAL_HEX8_MESSAGE((expected), (actual), NULL)

#define TEST_ASSERT_EQUAL_HEX16_MESSAGE(expected, actual, msg) \
    UnityAssertEqualHex((unsigned long)(expected) & 0xFFFFuL, \
                        (unsigned long)(actual) & 0xFFFFuL, 4, (msg), __LINE__)
#define TEST_ASSERT_EQUAL_HEX16(expected, actual) \
    TEST_ASSERT_EQUAL_HEX16_MESSAGE((expected), (actual), NULL)

#define TEST_ASSERT_FLOAT_WITHIN_MESSAGE(delta, expected, actual, msg) \
    UnityAssertFloatWithin((double)(delta), (double)(expected), (double)(actual), \
                           (msg), __LINE__)
#define TEST_ASSERT_FLOAT_WITHIN(delta, expected, actual) \
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE((delta), (expected), (actual), NULL)

/** 相对容差 1e-5 的浮点相等断言 (对齐官方 Unity 的 TEST_ASSERT_EQUAL_FLOAT 语义) */
#define TEST_ASSERT_EQUAL_FLOAT_MESSAGE(expected, actual, msg)                       \
    UnityAssertFloatWithin(                                                          \
        ((double)((expected) < 0 ? -(expected) : (expected)) * 1e-5) + 1e-6,          \
        (double)(expected), (double)(actual), (msg), __LINE__)
#define TEST_ASSERT_EQUAL_FLOAT(expected, actual) \
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE((expected), (actual), NULL)

#define TEST_ASSERT_EQUAL_MEMORY_MESSAGE(expected, actual, len, msg) \
    UnityAssertEqualMemory((expected), (actual), (size_t)(len), (msg), __LINE__)
#define TEST_ASSERT_EQUAL_MEMORY(expected, actual, len) \
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE((expected), (actual), (len), NULL)

#define TEST_ASSERT_NULL_MESSAGE(ptr, msg) \
    UnityAssertPointer((ptr), 1, (msg), __LINE__)
#define TEST_ASSERT_NULL(ptr)     TEST_ASSERT_NULL_MESSAGE((ptr), NULL)
#define TEST_ASSERT_NOT_NULL_MESSAGE(ptr, msg) \
    UnityAssertPointer((ptr), 0, (msg), __LINE__)
#define TEST_ASSERT_NOT_NULL(ptr) TEST_ASSERT_NOT_NULL_MESSAGE((ptr), NULL)

#ifdef __cplusplus
}
#endif

#endif /* FIRMWARE_COMMON_UNITY_H */
