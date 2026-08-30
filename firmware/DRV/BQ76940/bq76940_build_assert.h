#ifndef BQ76940_BUILD_ASSERT_H
#define BQ76940_BUILD_ASSERT_H

#define BQ76940_BUILD_ASSERT(condition, name) \
    typedef char bq76940_build_assert_##name[(condition) ? 1 : -1]

#endif /* BQ76940_BUILD_ASSERT_H */
