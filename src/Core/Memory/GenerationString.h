#pragma once
#include "Core/Memory/BufferAllocationBudget.h"
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cutum
{
// One fresh derived lookup string, borrowed only within this object's scope.
// Reserve before construction and free text before publishing available credit.
class UGenerationString
{
  static size_t Length(std::initializer_list<std::string_view> parts) {
    size_t length=0;
    const auto maximum=std::string{}.max_size();
    for(auto part:parts) {
      if(part.size()>maximum-length) throw std::length_error("Generation lookup name too long");
      length+=part.size();
    }
    return length;
  }
  static size_t AllocationBound(size_t length) {
    if(length<=std::string{}.capacity()) return 0;
    if(length>std::numeric_limits<size_t>::max()/2-1) throw std::length_error("Generation name reservation overflow");
    // Fresh reserve/append capacity is below twice(size+1) on desktop STLs.
    return 2*(length+1);
  }
  struct Reservation {
    BufferBudgetContext Owner;
    size_t Bytes;
    explicit Reservation(size_t length) : Owner(ActiveBufferBudget),Bytes(AllocationBound(length))
    {if(Owner.budget && Bytes) Owner.budget->Reserve(*Owner.job,Bytes);}
    ~Reservation() {if(Owner.budget && Bytes) Owner.budget->Release(*Owner.job,Bytes);}
    Reservation(const Reservation &)=delete;
    Reservation &operator=(const Reservation &)=delete;
  } Credit;
  std::string Text;
  static std::string Build(std::initializer_list<std::string_view> parts) {
    std::string result;
    result.reserve(Length(parts));
    for(auto part:parts) if(!part.empty()) result.append(part.data(),part.size());
    return result;
  }
public:
  explicit UGenerationString(std::initializer_list<std::string_view> parts)
      : Credit(Length(parts)),Text(Build(parts)) {}
  UGenerationString(const UGenerationString &)=delete;
  UGenerationString &operator=(const UGenerationString &)=delete;
  const std::string &Get() const noexcept {return Text;}
};
} // namespace cutum
