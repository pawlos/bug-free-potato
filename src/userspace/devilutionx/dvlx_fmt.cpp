/*
 * dvlx_fmt.cpp -- Compile the fmtlib implementation for DevilutionX.
 */

#define FMT_THROW(x) do { while(1); } while(0)
#define FMT_STATIC_THOUSANDS_SEPARATOR ','
#define FMT_USE_LOCALE 0

#include <fmt/format.h>
#include <fmt/format-inl.h>

/* Explicitly instantiate the template functions that format.h declares
   as extern templates but format-inl.h doesn't instantiate. */
namespace fmt {
inline namespace v12 {
namespace detail {
template FMT_API auto thousands_sep_impl<char>(locale_ref) -> thousands_sep_result<char>;
template FMT_API auto decimal_point_impl<char>(locale_ref) -> char;
} // namespace detail
} // namespace v12
} // namespace fmt
