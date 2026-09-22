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
  /// N04 autopsy: sample ≤8 FullyDark stalled columns.
  int stalled_sample_n{0};
  int stalled_sample_has_ticket_n{0};
  int stalled_sample_pending_light_n{0};
};

/// Classify why a focus column counts as visible-black.
/// LegalDarkNoRepair: FullyDark + matching light revs + no ticket/progress/sticky
/// and no pending light→mesh replace (true cave / no repair demand).
/// Equal-rev is required: mismatched revs ⇒ debt (FullyDarkNoTicket), not LegalDark.
inline VisibleBlackCause ClassifyVisibleBlackColumn(bool stale_dark,
                                                    bool fully_dark,
                                                    bool has_ticket,
                                                    bool has_progress,
                                                    bool sticky,
                                                    bool pending_replace,
                                                    bool light_revs_match = true)
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
  // A21 residual R1: without matching revs this is repair debt, not legal cave.
  if (!pending_replace && light_revs_match)
  {
    return VisibleBlackCause::LegalDarkNoRepair;
  }
  return VisibleBlackCause::FullyDarkNoTicket;
}

/// Helper: observational equal-rev FullyDark with no repair owner.
inline bool IsEqualRevLegalDark(bool fully_dark, bool has_ticket,
                                bool has_progress, bool sticky,
                                bool pending_replace, bool light_revs_match)
{
  return ClassifyVisibleBlackColumn(false, fully_dark, has_ticket, has_progress,
                                    sticky, pending_replace,
                                    light_revs_match) ==
         VisibleBlackCause::LegalDarkNoRepair;
}

} // namespace cutum

#endif // VISIBLEBLACKATTRIBUTION_H
