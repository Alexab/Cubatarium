#pragma once

#include "Core/Memory/WindowsCrtLocaleStorage.h"
#include "Test/WindowsCrtThreadFixture.h"
#include <array>
#include <cstring>
#include <cwchar>
#include <memory>

namespace cutum::windows_crt_locale_fixture
{
#if CUTUM_WINDOWS_STATIC_THREAD_CRT_PROFILE
using io_codec_fixture::Require;
using native_thread_stack_fixture::ParkedThread;
using windows_crt_thread_fixture::CountTrackedBusyBlocks;
using Locale = std::unique_ptr<__crt_locale_pointers, decltype(&_free_locale)>;

inline Locale Current()
{
  Locale current(_get_current_locale(), &_free_locale);
  Require(bool(current), "obtain a retained CRT locale reference");
  return current;
}

inline std::size_t RequestSize(const void *block)
{
  const auto heap = GetProcessHeap();
  Require(block && HeapValidate(heap, 0, block), "validate a known CRT locale allocation");
  const auto bytes = HeapSize(heap, 0, block);
  Require(bytes != SIZE_MAX, "observe the actual CRT locale request");
  return bytes;
}

struct LocaleTransition
{
  Locale shared, first, second, restored;
};

inline LocaleTransition ChangeWorkerLocale()
{
  auto shared = Current();
  const auto previous = _configthreadlocale(_ENABLE_PER_THREAD_LOCALE);
  Require(previous != -1, "enable the worker's private locale");
  struct RestoreMode
  {
    int mode;
    ~RestoreMode() { _configthreadlocale(mode); }
  } restore{previous};
  Require(_wsetlocale(LC_NUMERIC, L"de-DE") != nullptr, "initialize a private numeric locale");
  auto first = Current();
  auto second = Current();
  Require(_wsetlocale(LC_NUMERIC, L"C") != nullptr, "restore the worker's numeric locale");
  auto restored = Current();
  return {std::move(shared), std::move(first), std::move(second), std::move(restored)};
}

template<class T> inline const T *ReadLocaleMember(const __crt_locale_data *locale, std::size_t offset)
{
  const T *member{};
  std::memcpy(&member, reinterpret_cast<const char *>(locale) + offset, sizeof(member));
  return member;
}
#endif

inline nlohmann::json Run()
{
#if CUTUM_WINDOWS_STATIC_THREAD_CRT_PROFILE
  using namespace windows_crt_locale_profile;
  const auto observed = ObserveWindowsCrtLocale();
  auto startup = Current();
  const auto shellBytes = RequestSize(startup.get());
  if (!observed.sampled || observed.privateMode ||
      (!observed.initialLocale && observed.localeRootRequest != LocaleBytes) ||
      (!observed.initialMultibyte && observed.multibyteRootRequest != MultibyteBytes) ||
      observed.snapshotRequestBytes != PairBytes || shellBytes != PairBytes)
  {
    const nlohmann::json measured{{"sampled", observed.sampled}, {"initial_locale", observed.initialLocale},
        {"initial_multibyte", observed.initialMultibyte}, {"private_mode", observed.privateMode},
        {"locale_root", observed.localeRootRequest}, {"multibyte_root", observed.multibyteRootRequest},
        {"snapshot_request", observed.snapshotRequestBytes}, {"shell_request", shellBytes}};
    throw std::runtime_error("Observe the main CRT locale: " + measured.dump());
  }
  const auto numericName = _wsetlocale(LC_NUMERIC, nullptr);
  Require(numericName && std::wcscmp(numericName, L"C") == 0, "verify the main numeric locale is C");
  auto main = Current();
  Require(main->locinfo != startup->locinfo && main->mbcinfo == startup->mbcinfo &&
              RequestSize(main->locinfo) == LocaleBytes,
          "the UCRT query publishes a replacement locale while retaining the shared code-page block");
  startup.reset();
  const auto mainPairCurrent = [&] {
    const auto current = Current();
    return current->locinfo == main->locinfo && current->mbcinfo == main->mbcinfo;
  };
  for (const void *base : {static_cast<const void *>(&__acrt_initial_locale_data),
                           static_cast<const void *>(&__acrt_initial_multibyte_data)})
  {
    MEMORY_BASIC_INFORMATION region{};
    Require(VirtualQuery(base, &region, sizeof(region)) && region.Type == MEM_IMAGE &&
                region.AllocationBase == GetModuleHandleW(nullptr),
            "the initial locale objects are backed by the linked image");
  }
  std::array<std::uintptr_t, 3> explicitBlocks{};
  {
    Locale explicitC(_wcreate_locale(LC_ALL, L"C"), &_free_locale);
    Require(explicitC && explicitC->locinfo != main->locinfo && explicitC->mbcinfo != main->mbcinfo &&
                RequestSize(explicitC.get()) == PairBytes && RequestSize(explicitC->locinfo) == LocaleBytes &&
                RequestSize(explicitC->mbcinfo) == MultibyteBytes,
            "an explicitly created C locale owns a shell and two independent payloads");
    explicitBlocks = {reinterpret_cast<std::uintptr_t>(explicitC.get()),
        reinterpret_cast<std::uintptr_t>(explicitC->locinfo), reinterpret_cast<std::uintptr_t>(explicitC->mbcinfo)};
    Require(CountTrackedBusyBlocks(explicitBlocks) == explicitBlocks.size(), "observe all three explicit locale blocks live");
  }
  Require(!CountTrackedBusyBlocks(explicitBlocks), "retire the explicit locale after freeing its last reference");
  const std::wstring invalidName(300, L'!');
  Locale invalid(_wcreate_locale(LC_ALL, invalidName.c_str()), &_free_locale);
  Require(!invalid, "reject an invalid explicit locale");
  Require(mainPairCurrent(), "the failed explicit locale leaves the main roots in place");
  std::promise<LocaleTransition> ready;
  auto completed = ready.get_future();
  auto held = [&] {
    const ParkedThread worker([&](std::future<void> &released) {
      try { ready.set_value(ChangeWorkerLocale()); }
      catch (...) { ready.set_exception(std::current_exception()); }
      released.wait();
    });
    Require(completed.wait_for(std::chrono::seconds(8)) == std::future_status::ready,
            "publish the worker's retained locale references");
    auto sample = completed.get();
    Require(sample.shared.get() != main.get() && sample.shared->locinfo == main->locinfo &&
                sample.shared->mbcinfo == main->mbcinfo && RequestSize(sample.shared.get()) == PairBytes,
            "main and worker return distinct shells over the same locale and multibyte roots");
    Require(sample.first.get() != sample.second.get() && sample.first->locinfo == sample.second->locinfo &&
                sample.first->locinfo != main->locinfo && sample.first->mbcinfo == main->mbcinfo &&
                sample.restored->locinfo != sample.first->locinfo && mainPairCurrent(),
            "the private numeric change retains a distinct locale and shares its multibyte data");
    return sample;
  }();
  // Offsets verified from the exact SDK's __crt_locale_data layout.
  constexpr std::size_t lconvOffset = 248, intlRefOffset = 224, numericRefOffset = 232;
  const auto *locale = held.first->locinfo;
  const auto *conversion = ReadLocaleMember<lconv>(locale, lconvOffset);
  Require(conversion && std::strcmp(conversion->decimal_point, ",") == 0,
          "the retained private locale still holds its numeric format after the worker joins");
  const std::array<const void *, 9> addresses{locale, conversion,
      ReadLocaleMember<long>(locale, intlRefOffset), ReadLocaleMember<long>(locale, numericRefOffset),
      conversion->decimal_point, conversion->thousands_sep, conversion->grouping,
      conversion->_W_decimal_point, conversion->_W_thousands_sep};
  std::array<std::uintptr_t, 9> tracked{};
  std::array<std::size_t, 9> requests{};
  for (std::size_t i = 0; i < addresses.size(); ++i)
  {
    tracked[i] = reinterpret_cast<std::uintptr_t>(addresses[i]);
    requests[i] = RequestSize(addresses[i]);
  }
  Require(requests[0] == LocaleBytes && requests[1] == ConversionBytes &&
              requests[2] == sizeof(long) && requests[3] == sizeof(long),
          "the private locale owns the observed locale, conversion and reference blocks");
  const auto afterJoin = CountTrackedBusyBlocks(tracked);
  Require(afterJoin == tracked.size(), "both returned references retain the private numeric allocations");
  held.first.reset();
  const auto lastAlias = CountTrackedBusyBlocks(tracked);
  Require(lastAlias == afterJoin, "one remaining alias preserves the private numeric allocations");
  held.second.reset();
  const auto retired = CountTrackedBusyBlocks(tracked);
  Require(!retired && mainPairCurrent(), "free the private numeric allocations after the last alias");
  return {{"status", "PASS"}, {"shared_initial_named_bytes", observed.initialNamedBytes},
      {"snapshot_request", PairBytes}, {"explicit_c_root_requests", {PairBytes, LocaleBytes, MultibyteBytes}},
      {"main_locale_request", observed.localeRootRequest}, {"main_multibyte_request", observed.multibyteRootRequest},
      {"numeric_query_published_request", LocaleBytes},
      {"numeric_selected_requests", requests}, {"numeric_live_blocks_after_join", afterJoin},
      {"numeric_live_blocks_after_first_release", lastAlias}, {"numeric_live_blocks_after_last_release", retired},
      {"shared_initial_pair", true}, {"explicit_c_retired", true}, {"invalid_rejected", true}, {"main_locale_preserved", true}};
#else
  return {{"status", "UNSUPPORTED"}};
#endif
}
} // namespace cutum::windows_crt_locale_fixture
