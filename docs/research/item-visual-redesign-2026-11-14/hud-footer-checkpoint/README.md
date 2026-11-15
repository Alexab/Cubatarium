# HUD footer at all required display scales

The bottom hotbar and throw/bow hint now reserve the version/coordinate footer
row using scaled theme metrics. Primary and secondary hotbars use that reserved
margin. Info badges respect their widget clipping bounds; the coordinate badge
has a bounded width. An unconditional per-badge stderr trace was removed from
the normal render path.

The 1920x1080 UI 200% overlap recorded by the previous use checkpoint is fixed.
Native GLFW bow input runs pass at 1280x720 and 1920x1080, each at UI 100, 150
and 200 percent: 48 captured stages. All six ready-frame originals are archived;
the overview is a labelled footer crop. Full reports, frame hashes, source,
resource/config/world conservation and executable fingerprints are retained.
1920 UI 200, 1280 UI 200 and 1920 UI 150 originals were individually inspected.
This checks the HUD with a real charged bow and hint, not Item Lab or all final
lighting/view combinations.

The Windows native CI workflow now includes both-mode food/drink/bow callback
scenarios with exit-code checks; its existing artifact upload includes world
capture reports. The YAML parses locally. No remote CI run is claimed.
The full integrated Release regression passes after this GUI correction: 25
CTests, gameplay 59, ownership 57, ownership UI 18, player window 7, icons 209,
Item Lab 1041 and presentations 3660; the window profile also passes. Reports
are retained in `regression/`.
