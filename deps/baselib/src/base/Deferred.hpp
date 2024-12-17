#pragma once
#include <base/macro/AnonymousName.hpp>
#include <base/macro/ClassTraits.hpp>

#include <type_traits>
#include <utility>

namespace base {

template <typename Fn>
class Deferred {
  Fn fn;

 public:
  CLASS_NON_COPYABLE_NON_MOVABLE(Deferred)

  Deferred(Fn&& fn) : fn(std::forward<Fn>(fn)) {}
  ~Deferred() { fn(); }
};

}  // namespace base

#define DEFER_BLOCK const ::base::Deferred ANONYMOUS_NAME(_base_deferred) = [&]()