#ifndef VISIBLEBLACKATTRIBUTION_H
#define VISIBLEBLACKATTRIBUTION_H

namespace cutum
{

enum class VisibleBlackCause
{
  StaleDarkWithLitField,
  FullyDarkPendingRepair,
  FullyDarkNoTicket,
  FullyDarkStalledTicket,
  LegalDarkNoRepair,
};

struct VisibleBlackFocusCounts
{
  int focus_n{0};
  int no_ticket{0};
  int progress{0};
  int stalled{0};
  int stale_lit{0};
  int fully_dark_repair{0};
  int fully_dark_no_ticket{0};
  int fully_dark_stalled{0};
  int legal_dark{0};
};

/// Classify why a focus column counts as visible-black.
inline VisibleBlackCause ClassifyVisibleBlackColumn(bool stale_dark,
                                                    bool fully_dark,
                                                    bool has_ticket,
                                                    bool has_progress,
                                                    bool sticky,
                                                    bool pending_replace)
{
  if (!fully_dark)
  {
    return VisibleBlackCause::StaleDarkWithLitField;
  }
  if (has_ticket && !has_progress && !sticky)
  {
    return VisibleBlackCause::FullyDarkStalledTicket;
  }
  if (has_ticket || has_progress || sticky)
  {
    return VisibleBlackCause::FullyDarkPendingRepair;
  }
  if (!pending_replace)
  {
    return VisibleBlackCause::LegalDarkNoRepair;
  }
  return VisibleBlackCause::FullyDarkNoTicket;
}

} // namespace cutum

#endif // VISIBLEBLACKATTRIBUTION_H
