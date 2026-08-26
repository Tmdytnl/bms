#ifndef BMS_BUILD_ASSERT_H
#define BMS_BUILD_ASSERT_H

/* ARM Compiler 5 兼容的编译期断言；name 必须是当前 scope 唯一合法 C identifier。 */
#define BMS_BUILD_ASSERT(condition, name) \
    typedef char bms_build_assert_##name[(condition) ? 1 : -1]

#endif /* BMS_BUILD_ASSERT_H：include guard */
