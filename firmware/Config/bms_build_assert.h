#ifndef BMS_BUILD_ASSERT_H
#define BMS_BUILD_ASSERT_H

/*
 * ARM Compiler 5 compatible compile-time assertion.
 * The name argument must be a valid and unique C identifier in its scope.
 */
#define BMS_BUILD_ASSERT(condition, name) \
    typedef char bms_build_assert_##name[(condition) ? 1 : -1]

#endif /* BMS_BUILD_ASSERT_H */
