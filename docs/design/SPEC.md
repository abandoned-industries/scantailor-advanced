# ScanTailor Spectre interface — design spec

Process: `~/Downloads/designtodeviceplaybook.md` (design in numbered
revisions; the spec and boards for a revision are the only work order; the
owner judges on the real thing; a word said while looking at the real thing
is a spec change, written down with date and quote).

Data contract: `docs/design/current-ui-inventory.md`. No other data exists.
A board that wants a number the app does not have is flagged in "Open
contract items" below, not invented.

Boards: sources in `docs/design/rev1/*.dc.html` + `canvas.json`; assembled
by `docs/design/build-boards.sh`. Regenerate, never hand-edit renders.

---

## Revision 1 — 2026-09-01 — APPROVED by the owner 2026-09-01 ("yes")

### The rule

Each stage opens with one plain sentence about the page ("This page leans
1.3° to the left."), says what it did, offers one action, and keeps every
dial behind an "Adjust" fold. He judges by looking at the page, which is large, and
answers with one click. Nothing is lost: every control that exists today is
still there under Adjust, in today's words.

### Direction (settled by the designer, alternates shown beside)

Main candidate: **"Page first"** — three columns, page dominant, ask card
under the stage rail. Two low-fi alternates sit beside it on the canvas for
comparison only: **A "Tidy"** (today's layout, cleaned up, every control
still visible) and **B "One step"** (full-screen wizard, one stage per
screen). Names are fixed; never renumber.

### Vocabulary

- Stage names stay exactly as today (he uses them): Fix Orientation, Split
  Pages, Deskew, Page Box, Select Content, Margins, Finalize, Output, OCR,
  Export.
- Control labels under Adjust stay as today. One spelling everywhere:
  "Apply to…" (ellipsis character).
- Nothing in the default face uses internal words: no "batch", "filter",
  "threshold", "deviation", "coverage", "binarization". Those words may
  appear only inside Adjust.

### Palette and type (extends today's, one addition)

Light scheme only in Revision 1 (dark follows the same rules from the dark
palette in the inventory; not drawn yet).

| Token | Value | Source |
|---|---|---|
| window | #F0F0F0 | Light Window |
| base | #FCFCFC | Light Base |
| text | #303030 | WindowText |
| text-2 | #666666 | selection-indicator gray |
| text-disabled | #909090 | |
| border | #CCCCCC | Mid |
| border-strong | #B5B5B5 | Highlight |
| well | #C8C8C8 | image view background |
| dock-title | #E8E8E8 | |
| thumb-selected | #727272 | |
| thumb-leader | #5E5E5E | |
| warn | #D17A00 | Zotero warning |
| error | #F40000 | BrightText |
| **accent** | **#2D4FA3** (oklch ≈ 0.44 0.15 265) | NEW: the one primary action per screen; the current Link #0000FF, toned down |
| accent-hover | #24408A | |

- Corners: 0 px everywhere (today's language). Borders 1 px.
- Font: system-ui (SF Pro on macOS), fallbacks -apple-system, Helvetica
  Neue, Arial. Sizes: card heading (Review, Export only) 20 px / 600 (= today's 15 pt DemiBold);
  body 13 px / 400 / line-height 1.4; control labels 12 px; stage rail
  13 px; tiny meta 11 px; page counter in batch 40 px / 600.
- Controls: buttons 28 px tall, padding 0 14 px, 1 px border #CCC on base,
  text 13 px #303030. Primary button: accent background, white text, no
  border. Secondary text action: accent text, no border, no background.
  Checkbox 14 px square. Inputs 28 px, 1 px #CCC, base background.
- Icons: stroke SVG, 16 px grid, 1.5 px stroke, currentColor. Never emoji.

### Main window (1440 × 900)

Three columns, no toolbars, native menu bar not drawn.

1. **Stage column, 300 px, background window.**
   - Header strip 32 px, "Stages" 12 px / 600 uppercase-tracked? No: plain
     "Stages", dock-title background, border-bottom 1 px #CCC.
   - Stage rail: 10 rows × 30 px. Each row: 24 px number cell right-aligned
     #909090, name 13 px, right-hand 16 px status glyph: check (stage done
     for all pages), half-filled circle (partly done), nothing (untouched).
     Selected row: base background, 3 px accent bar on the left edge,
     name 600. No play button in the rail (the ask card holds the action).
   - **Ask card** below the rail, base background, border-top 1 px #CCC,
     padding 20 px, fills the rest of the column:
     1. (no heading; see Copy rule)
     2. Action row, gap 8: one primary button + one secondary text action.
     3. "Adjust" fold: a 13 px row with a chevron. Collapsible; the app
        remembers whether it was open or closed the last time, per stage,
        and opens it that way next time (R1.2). Open, it shows today's
        controls for the stage, today's labels, grouped as today, and the
        single "Apply to…" button at its end.
     5. Card footer, 11 px #666: scope ("Editing 3 pages" when multi-select
        is on, else "Page 37 of 212") and, when there is anything
        unprocessed, "211 pages waiting".
   - Yield rule: the fold is scrollable inside the card; the action row
     and footer never scroll away.
2. **Page column, fluid, background well.**
   - The page image centered, fitted, with 24 px inset. Overlays drawn by
     the stage (deskew guide lines, content box, margins) stay as today.
   - Bottom-left chip 11 px on base with 1 px #CCC: file name · "37 / 212".
   - Bottom-right chip: zone-mode icon (rectangle, 16 px; today's status
     bar item), zoom "Fit" and units; today's status-bar items (mouse
     position, image size) move here (R1.3).
3. **Thumbnail column, 220 px, background window.**
   - Header strip 32 px: "Pages" left; right: a 16 px icon row of today's
     rail (keep in view, selected-only, multi-select, sort order, deviation
     highlight, magnify/diminish) as 24 px hit targets, plus the B G M C
     color toggles as 20 px text pills.
   - Vertical strip, two columns of thumbnails 90 × 120 with 6 px gap, page
     number under each 11 px #666. Selected: 2 px #727272 ring; leader:
     #5E5E5E ring. Pages the current stage has not processed: 40% opacity.
   - Sort combo appears under the header only on stages that offer more
     than "Natural order".

### Copy rule (R1.1, owner-directed 2026-09-01)

The app does not talk. No sentences in the default face: no "This page
leans…", no "I straightened it…", no "About 2 minutes left". The one
exception is Export's heading "Your book is ready." Everything else on a
card is a control label, a number the app really has, or a stage name.

Ask card, every stage: (1) action row: primary "Process all pages",
secondary text action "This page only"; (2) "Adjust" fold, holding today's
controls with today's labels, open or closed as last left (R1.2); (3) footer with the
scope and the waiting count. No heading line above the action row (the
stage is already named in the rail).

Per-board exceptions:

| Board | Heading | Primary | Secondary |
|---|---|---|---|
| Review | "3 pages to check" (a count, 20 px / 600) | "All fine" | "Show all 212" |
| Batch | stage name "Deskew" 13 px #666 above the counter "40 of 212" | "Stop" | — |
| Export | "Your book is ready." | "Export PDF…" | — |

Review cards keep the page number and the measured value ("leans 4.8°") and
the two buttons "Fine" / "Fix". Batch loses the time estimate. Export loses
the facts line and the "stays undoable" line.

### Finalize card, Adjust open (board `Finalize`)

Under Adjust: a four-way segmented control with today's names Black and
White · Grayscale · Mixed · Color (28 px, selected = accent border 2 px and
600 text); "Auto White Balance" checkbox; "Picture detection sensitivity"
number field 8; then "Apply to…" as one 28 px popup button reading "This
page" with a chevron. The popup lists today's seven scopes in today's
words. The Detection Cache and Output Images groups stay under a second
fold "Advanced" inside Adjust.

### Batch (board `Batch`)

The page column is replaced while a batch runs; the stage column and
thumbnails stay visible (today they hide; Revision 1 keeps them so he can
watch pages tick in). Center, on well: stage name "Deskew" 13 px #666;
counter "40 of 212" 40 px / 600; a 4 px progress bar 480 px wide, accent on
#CCC; one 28 px secondary button "Stop"; checkbox "Beep when finished";
under it the system-load control as today: label "System load", a slider
with "Minimal" / "Maximal" end labels (R1.3). Processed
thumbnails go from 40% to full opacity as they finish.

### Review (board `Review`) — replaces the four summary dialogs

After a batch, when some pages fall outside the rest, the ask card
heads "3 pages to check". R1.3: the board shows the Margins case (today's
"Page Size Issue" dialog) so every action maps to one that exists. Each
page card carries the page number, its size and deviation ("152 × 228 mm ·
+31%"), and two buttons: "Fine" and a popup "Fix ▾" listing today's actions
in today's words: "Jump to page", "Detach from aggregate size", "Go to
Split Pages". The popup is drawn open on the first card. The ask card's
Adjust is open and holds "Sensitivity: 30%" slider and "Detach all
listed". The page column
shows the three pages as 3 large cards side by side (each with page number
and the app's note in 11 px, e.g. "leans 4.8°"), each card with two 28 px
buttons: "Fine" (secondary) and "Fix" (primary outline). Primary in the
card: "All fine"; secondary: "Show all 212". The threshold slider and
Jump/Force/Detach buttons live under Adjust.

### Export (board `Export`)

Ask card: heading "Your book is ready."; primary "Export PDF…" pinned at
the card's bottom above the footer (never scrolls away). Zotero line above
the button when a loop project is open: a 16 px check icon + "Back to
Zotero after export" checkbox checked, and status "Zotero: running" in
#666. Adjust holds Book Metadata (Title, Author(s), Role, Year, Publisher,
Place, ISBN, Language, "Guess from scan", "Look up ISBN", "Suggest file
name from metadata"), PDF Export (No DPI limit, Max Image DPI, Compress
grayscale, JPEG Quality), Output Resolution. The page column shows the
first page as the PDF cover with the file name under it.

### Start window (board `Start`, 720 × 520)

Background base. Margins 40. Top: app icon 48 px + "ScanTailor Spectre"
20 px / 600 + "2.0b2" 11 px #666. Three primary-looking rows 44 px tall,
full width, 1 px #CCC border, icon 16 px + label 15 px / 600: "Import
PDF", "Import Folder", "Open Project"; ⌘I / — / ⌘O shown right-aligned
#909090. Then "Recent Projects" 13 px / 600 #666 and a list of rows 36 px:
project name 13 px / 600 + parent folder 11 px #666 (that is all the recent
list knows). When a Zotero loop project is waiting, a first row on base
with accent left bar: "From Zotero: <item title>" + "Clean this scan"
primary.

### Alternates (page "Alternates", low-fi, 720 × 450 each)

- **A "Tidy"**: today's three panes; options widget stays fully visible
  but grouped, one Apply to… per stage, play button becomes a labeled
  "Process all" button above the options. Tradeoff: nothing to learn, but
  the page stays small and every dial competes for attention.
- **B "One step"**: one stage per full screen, giant page, a single
  question at the bottom, Next/Back. Tradeoff: fastest for a clean book,
  slowest when a book needs many back-and-forths between stages.

### Open contract items (flagged, not invented)

Owner asked 2026-09-01 "does your plan involve any loss of
functionality?" Answer: none by design. Three gaps were found and drawn in
R1.3 (Review actions, system-load slider, zone-mode icon). Still to map,
stage by stage, before code: the Review "Fix" popup contents for Split
Pages (Force two-page / Force single page / Reset to auto layout), Page
Box (Disable page box) and Select Content (Preserve layout).

- Recent projects: the app stores only the path. Page count and last stage
  reached are not available without opening the project file. Boards show
  name + folder only.
- Review after Output would need a per-page confidence from Output. Today
  only Split, Page Box, Select Content and Margins produce outlier lists.

### Not in Revision 1

Dark scheme boards, Settings, Project Files dialog, Fix DPI, Relinking,
menus, keyboard map (13 bare-letter bindings today; to be redesigned once
the direction is approved).

### Revision log

- R1 2026-09-01: drafted from the playbook and the inventory. Awaiting the
  owner's word on the direction.
- R1 2026-09-01, owner: "the 'app now speaks first' is slop. we don't need
  it." The slogan is removed from the spec, the canvas note and reports; the
  behavior it named is unchanged.
- R1.1 2026-09-01, owner: "remove ALL the app speaks now text. it's slop. get
  rid of it. with the except of the book is ready." Every truth/did sentence
  and chatty button label removed from spec and boards; "Your book is ready."
  kept on Export. See Copy rule.
- R1.2 2026-09-01, owner, looking at the three pictures: "that's fine, but
  adjust is important. i think it should be collapsible. the default state
  is whatever was last used in the app. we are not ready to fork yet."
  Direction: the three-column window (was "Page first"). Adjust stays
  collapsible and reopens as last left, per stage. No code branch yet.
- R1.3 2026-09-01, owner: "yes." to placing the three missing functions.
  Review redrawn on the Margins case with a "Fix" popup of today's actions;
  Batch gains the system-load slider; the page chip gains the zone-mode icon.
- R1 closed 2026-09-01, owner: "yes". No code work until he separately says go.
