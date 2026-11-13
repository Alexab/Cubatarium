# Crafting

Cubatarium crafting is a small Game module, not an Influence channel.

- Data: `content/recipes/*.json` (shapeless inputs and one output).
- Logic: `src/Game/Crafting/RecipeRegistry.cpp` (`LoadFromDirectory`, `TryCraft`).
- UI: `CraftingScreen` (opened from a `crafting_table` block) and `AnvilScreen` (repair of the active hotbar item).
- Commands: `/craft`, `/give`, `/repair`.

`TryCraft` consumes inputs from creature storage and calls `AddItem` for the output. It does not place the result into the hotbar.

Repair materials on metal tools use block ids that actually drop (`mtg_steelblock`, `mtg_copperblock`, `mtg_goldblock`), matching the recipes.
