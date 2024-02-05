#ifndef TOOL_H
#define TOOL_H

#include "log.h"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <functional>
#include <iterator>
#include <tuple>
#include <type_traits>
#include <utility>
#include <ranges>
#include <vector>

namespace tool {

namespace ranges = std::ranges;
namespace views = std::views;

template<uint32_t index, typename Callable>
struct FuncArgT;
// todo: 对于重载函数而言有bug
template<uint32_t index, typename Func>
using FuncArg = typename FuncArgT<index, Func>::type;

// 用于成员函数类型 (需要考虑 const 和非 const)
template<uint32_t index, typename C, typename R, typename... Args>
struct FuncArgT<index, R(C::*)(Args...) const>
{
	using type = std::tuple_element_t<index, std::tuple<Args...>>;
};
template<uint32_t index, typename C, typename R, typename... Args>
struct FuncArgT<index, R(C::*)(Args...)>
{
	using type = std::tuple_element_t<index, std::tuple<Args...>>;
};

// 用于函数指针
template<uint32_t index, typename R, typename... Args>
struct FuncArgT<index, R(*)(Args...)>
{
	using type = std::tuple_element_t<index, std::tuple<Args...>>;
};
// 用于函数
template<uint32_t index, typename R, typename... Args>
struct FuncArgT<index, R(Args...)>
{
	using type = std::tuple_element_t<index, std::tuple<Args...>>;
};

// 用于持有 operator() 的类
template<uint32_t index, typename Callable>
struct FuncArgT
{
	using type = FuncArgT<index, decltype(&Callable::operator())>::type;
};

// type list
template <typename... P> 
struct TypePack {
  // 这种模板嵌套说明 T 可以是一个模板
  template <template <typename...> typename T> using apply = T<P...>;

  template <typename... T> using merge = TypePack<P..., T...>;
};

template <typename T, typename Seq>
struct RepeatS;
template <typename T, size_t... Is>
struct RepeatS <T, std::index_sequence<Is...>> {
  template<size_t N>
  using element = T;
  using type = TypePack<element<Is>...>;
};
template<typename T, size_t N>
using Repeat = RepeatS<T, std::make_index_sequence<N>>::type;

template <typename TypePack, size_t N>
using Index = typename std::tuple_element<
    N, typename TypePack::template apply<std::tuple>>::type;

template <typename TypePack, typename T> 
struct InsertS;

template <typename TypePack, typename T>
using Insert = InsertS<TypePack, T>::type;

template <typename... TypeList, typename T>
struct InsertS<TypePack<TypeList...>, T> {
  using type = TypePack<TypeList..., T>;
};


template <typename TypePack, template <typename...> typename Mapper>
struct MapToS;

template <typename TypePack, template <typename...> typename Mapper>
using MapTo = MapToS<TypePack, Mapper>::type;

template <typename... TypeList, template <typename...> typename Mapper>
struct MapToS<TypePack<TypeList...>, Mapper> {
  using type = TypePack<Mapper<TypeList>...>;
};

template <typename TypePack1, typename TypePack2> 
struct MergeS;

template <typename TypePack1, typename TypePack2>
using Merge = MergeS<TypePack1, TypePack2>::type;

template <typename... TypeList1, typename... TypeList2>
struct MergeS<TypePack<TypeList1...>, TypePack<TypeList2...>> {
  using type = TypePack<TypeList1..., TypeList2...>;
};


template<typename TypePack, size_t index>
struct RemoveFrontS;

template<typename TypePack, size_t index>
using RemoveFront = RemoveFrontS<TypePack, index>::type;

template<typename Arg, typename... Args>
struct RemoveFrontS<TypePack<Arg, Args...>, 1>{
	using type = TypePack<Args...>;
};

template<size_t index, typename Arg, typename... Args>
struct RemoveFrontS<TypePack<Arg, Args...>, index>{
	using type = RemoveFront<TypePack<Args...>, index-1>;
};

template<typename R, typename V>
concept RangeOf = std::same_as<ranges::range_value_t<R>, V>;

namespace {

template <typename Adaptor, typename... Args> 
struct AdaptorClosure {
  Adaptor adaptor_;
  std::tuple<Args...> args_;
  decltype(auto) operator()(auto&& range) {
    return std::apply(
        [this, &range](auto &&...args) -> decltype(auto) {
          return adaptor_(std::forward<decltype(range)>(range), std::forward<decltype(args)>(args)...);
        },
        args_);
  }

  AdaptorClosure(Adaptor adaptor, auto &&...args)
      : adaptor_(adaptor), args_(std::forward<decltype(args)>(args)...) {}
};

// 构造函数的参数用万能引用，并推导得到Args为去掉引用的类型
template <typename Adaptor, typename... Args>
AdaptorClosure(Adaptor adaptor, Args &&...args)
    -> AdaptorClosure<Adaptor, std::remove_reference_t<Args>...>;

template <typename... Args>
auto operator|(auto&& range, AdaptorClosure<Args...> closure) {
  return closure(std::forward<decltype(range)>(range));
}

// example
struct MyTake {
  auto operator()(auto &&range, int n) {
    return views::take(views::all(std::forward<decltype(range)>(range)), n);
  }
  auto operator()(int n) {
    return AdaptorClosure{*this, n};
  }
};

void test_AdaptorClosure(){
  std::array arr = {1, 2, 3};
  auto view = arr | MyTake{}(2);
  auto view2 = MyTake{}(view, 2);
}

// CRTP 模式，辅助无参数adaptor
template <typename Derived>
struct AdaptorClosureHelper {};

template <typename Adaptor>
  requires std::is_base_of_v<AdaptorClosureHelper<Adaptor>, Adaptor>
auto operator|(auto&& range, Adaptor closure) {
  return closure(std::forward<decltype(range)>(range));
}

struct EnumerateAdaptor : AdaptorClosureHelper<EnumerateAdaptor> {
  template <ranges::sized_range Range> 
  auto operator()(Range &&range) {
    return views::zip(views::iota(0u, range.size()), range);
  }
};

template <ranges::forward_range View,
          std::indirect_binary_predicate<ranges::iterator_t<View>,
                                         ranges::iterator_t<View>>
              Pred>
  requires ranges::view<View> && std::is_object_v<Pred>
class ChunkByView : public ranges::view_interface<ChunkByView<View, Pred>> {
private:
  using Element = ranges::range_value_t<View>;
  using BaseIterator = ranges::iterator_t<View>;
  using SubRange = ranges::subrange<BaseIterator>;

  View base_;
  Pred pred_;

public:
  class Sentinel {};
  class Iterator {
    // 指向一个子范围（惰性的），当解引用时需要得到一个子范围（除非是end），自增操作需要寻找下一个子范围
  private:
    View* base_;
    BaseIterator left_;
    BaseIterator right_;
    Pred* pred_;

  public:
    Iterator(View &base, Pred &pred)
        : base_(&base), left_(base.begin()), right_(left_), pred_(&pred) {
      nextSubRange();
    }
    Iterator() = default;

    using difference_type = std::ptrdiff_t;
    using value_type = SubRange;
    using reference_type = value_type;
    auto nextSubRange() {
      left_ = right_;
      right_ = ranges::adjacent_find(
          ranges::subrange(left_, base_->end()), [this](auto &&a, auto &&b) {
            return !std::invoke(*pred_, std::forward<decltype(a)>(a),
                                std::forward<decltype(b)>(b));
          });
      if (right_ != base_->end()) {
        ++right_;
      }
    }
    auto prevSubRange() {
      right_ = left_;
      left_ = ranges::adjacent_find(
                  ranges::subrange(base_->begin(), left_) | views::reverse,
                  [this](auto &&a, auto &&b) {
                    return std::invoke(*pred_, std::forward<decltype(a)>(a),
                                       std::forward<decltype(b)>(b));
                  })
                  .base();
      if (left_ != base_->begin()) {
        --left_;
      }
    }
    auto operator++() -> Iterator & {
      nextSubRange();
      return *this;
    }
    auto operator++(int) -> Iterator {
      auto old = *this;
      nextSubRange();
      return old;
    }
    auto operator--() -> Iterator & {
      prevSubRange();
      return *this;
    }
    auto operator--(int) -> Iterator {
      auto old = *this;
      prevSubRange();
      return old;
    }
    friend auto operator==(const Iterator &a, const Iterator &b) -> bool {
      return a.left_ == b.left_;
    }
    friend auto operator==(const Iterator &a, Sentinel) -> bool {
      return a.left_ == a.base_->end();
    }

    auto operator*() const -> reference_type { return value_type{left_, right_}; }
  };
  auto begin() { return Iterator{base_, pred_}; }
  auto end() { return Sentinel{}; }

  ChunkByView(View view, Pred pred)
      : base_(std::move(view)), pred_(std::move(pred)) {}
};

struct ChunkByAdaptor {
  auto operator()(auto &&range, auto pred) {
    return ChunkByView(views::all(std::forward<decltype(range)>(range)), pred);
  }
  auto operator()(auto pred) {
    return AdaptorClosure{*this, pred};
  }
};

}

inline EnumerateAdaptor enumerate = EnumerateAdaptor{};
inline ChunkByAdaptor chunkBy = ChunkByAdaptor{};

// todo: 增加灵活性
template<typename Derived, typename ValueType, typename ReferenceType = ValueType>
class IteratorHelper {
public:
  using value_type = ValueType;
  using reference_type = ReferenceType;
  using difference_type = std::ptrdiff_t;

  decltype(auto) base() const {
    return static_cast<const Derived&>(*this).*Derived::baseMember();
  }
  decltype(auto) base() {
    return static_cast<Derived&>(*this).*Derived::baseMember();
  }
  decltype(auto) self() {
    return static_cast<Derived&>(*this);
  }
  decltype(auto) self() const {
    return static_cast<const Derived&>(*this);
  }

  auto operator*() const -> reference_type {
    return static_cast<const Derived>(static_cast<const Derived&>(*this)).deref();
  }

  auto operator++() -> Derived& {
    base()++;
    return static_cast<Derived&>(*this);
  }
  auto operator++(int) -> Derived {
    auto old = static_cast<Derived&>(*this);
    base()++;
    return old;
  }

  auto operator--() -> Derived & {
    base()--;
    return self();
  }
  auto operator--(int) -> Derived {
    auto old = self();
    base()--;
    return old;
  }
  

  auto operator+=(difference_type n) -> Derived & {
    base() += n;
    return self();
  }
  friend auto operator+(const Derived &a, difference_type n) -> Derived {
    return a.base() + n;
  }
  friend auto operator+(difference_type n, const Derived &a) -> Derived {
    return a.base() + n;
  }
  auto operator-=(difference_type n) -> Derived & {
    base() -= n;
    return self();
  }
  friend auto operator-(const Derived &a, difference_type n) -> Derived {
    return a.base() - n;
  }
  friend auto operator-(difference_type n, const Derived &a) -> Derived {
    return a.base() - n;
  }

  auto operator[](difference_type n) const -> reference_type {
    return self().at(n);
  }
  
  friend auto operator==(const Derived &a, const Derived &b) -> bool {
    return a.base() == b.base();
  }
  friend auto operator!=(const Derived &a, const Derived &b) -> bool {
    return a.base() != b.base();
  }

  friend auto operator<(const Derived &a, const Derived &b) -> bool {
    return a.base() < b.base();
  }
  friend auto operator<=(const Derived &a, const Derived &b) -> bool {
    return a.base() <= b.base();
  }
  friend auto operator>(const Derived &a, const Derived &b) -> bool {
    return a.base() > b.base();
  }
  friend auto operator>=(const Derived &a, const Derived &b) -> bool {
    return a.base() >= b.base();
  }

  friend auto operator-(const Derived &a, const Derived &b) -> difference_type {
    return a.base() - b.base();
  }
  
};


// 给一个range，返回一个排序后的新range，该range为了省空间只会存储排序后的迭代器
template <typename Range, class Comp = ranges::less, class Proj = std::identity>
  requires ranges::random_access_range<Range> && ranges::viewable_range<Range>
class SortedView : public ranges::view_interface<SortedView<Range, Comp, Proj>>  {
private:
  using BaseView = views::all_t<Range>;
  using BaseIterator = ranges::iterator_t<Range>;
  using IndexRange = std::vector<BaseIterator>;
  using IndexIterator = ranges::iterator_t<IndexRange>;

  BaseView view_;
  IndexRange index_;

public:
  SortedView(const SortedView&) = delete;
  SortedView(SortedView&&) = default;
  SortedView& operator=(const SortedView&) = delete;
  SortedView& operator=(SortedView&&) = default;

  class Iterator: public IteratorHelper<Iterator,std::iter_value_t<BaseIterator>> {
  private:
    IndexIterator index_iter;
    using Base = IteratorHelper<Iterator,std::iter_value_t<BaseIterator>>;
    friend Base;
  public:
    using typename Base::difference_type;
    using typename Base::value_type;
    using typename Base::reference_type;
    Iterator(IndexIterator index_it) : index_iter(index_it) {}
    Iterator() = default;
  private:
    constexpr static IndexIterator Iterator::* baseMember() {
      return &Iterator::index_iter;
    }
    auto deref() const -> reference_type {
      return **index_iter;
    }
    auto at(difference_type n) const -> reference_type {
      return *index_iter[n];
    }
  };
  auto begin() { return Iterator{index_.begin()}; }
  auto end() { return Iterator{index_.end()}; }

  SortedView(Range &&range, Comp comp = {}, Proj proj = {})
      : view_(views::all(range)), index_(view_.size()) {
    int j = 0;
    for (auto i = view_.begin(); i != view_.end(); i++, j++) {
      index_[j] = std::move(i);
    }
    ranges::sort(index_, std::move(comp), [this, proj](auto iter) { return proj(*iter); });
  }
};
template<typename Range, class Comp = ranges::less, class Proj = std::identity>
SortedView(Range &&range, Comp comp = {}, Proj proj = {}) -> SortedView<Range, Comp, Proj>;

inline void test_EnumerateAdaptor(){
  std::array arr {"abc", "eddd"};
  auto brr = arr | EnumerateAdaptor{};
  decltype(auto) c = *brr.begin();
  int j = 0;
  for(auto [i, e]: arr | EnumerateAdaptor{}) {
    j++;
  }
  check_throwf(j == 2, "");
}

inline void test_SortedRange() {
  static_assert(ranges::view<SortedView<std::vector<int>>>);
  static_assert(ranges::random_access_range<SortedView<std::vector<int>>>);
  static_assert(std::random_access_iterator<SortedView<std::vector<int>>::Iterator>);
  static_assert(!std::copyable<SortedView<std::vector<int>>>);
  auto view = views::iota(0, 10);
  auto view2 = view | views::reverse;
  auto view3 =
      SortedView{view, [](auto a, auto b){return a > b;}, [](auto a){return a + 10;}};
  auto view4 =
      SortedView{view, {}, [](auto a){return 10 - a;}};
  debug(view2);
  check_throwf(ranges::all_of(views::zip(view2, std::move(view3)),
                              [](auto a) { return a.first == a.second; }),
               "test_SortedRange");
  check_throwf(ranges::all_of(views::zip(view2, std::move(view4)),
                              [](auto a) { return a.first == a.second; }),
               "test_SortedRange");
}


inline void test_ChunkBy(){
  static_assert(ranges::forward_range<ChunkByView<views::all_t<std::vector<int>>, std::ranges::not_equal_to>>);
  static_assert(ranges::bidirectional_range<ChunkByView<views::all_t<std::vector<int>>, std::ranges::not_equal_to>>);
  // todo
  // static_assert(ranges::common_range<ChunkByView<views::all_t<std::vector<int>>, std::ranges::not_equal_to>>);
  std::array arr {1, 2, 3, 2, 3, 1, 5 ,10, 9, 8};
  auto view = chunkBy(arr, std::less{});
  auto i = view.begin();
  for(auto _ : views::iota(0, 5)) {
    debug(*i);
    i++;
  }
  check_throwf(i == view.end(), "i == view.end()");
  i--;
  for(auto _ : views::iota(0, 4)) {
    debug(*i);
    i--;
  }
  debug(*i);
  check_throwf(i == view.begin(), "i == view.begin()");
}

} // namespace tool

#endif // TOOL_H
