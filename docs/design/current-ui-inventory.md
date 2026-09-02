# ScanTailor Spectre — current UI inventory (2026-09-01)

Lifted from source (`.ui`, `.cpp/.mm`, `.qss`, resources), not from memory.
This is the data contract for the redesign boards: what the app actually
shows and knows today. Version at survey time: 2.0b2.

## 1. Startup window

`src/app/StartupWindow.cpp` — a plain `QWidget` window, `resize(500, 400)`,
holding one `NewOpenProjectPanel` built entirely in C++ (the `.ui` file is
compiled but unused; its values do not match what ships).

Panel, margins 40 all round, spacing 8, top-left aligned:

| # | Element | Text | Font |
|---|---|---|---|
| 1 | ClickableLabel | "Import PDF" | 15 pt DemiBold |
| 2 | ClickableLabel | "Import Folder" | 15 pt DemiBold |
| 3 | ClickableLabel | "Open Project" | 15 pt DemiBold |
| 4 | spacing 20 | | |
| 5 | QLabel | "Recent Projects" | 15 pt DemiBold |
| 6 | list, spacing 4 | project `completeBaseName()`, tooltip = full path; or "No recent projects" | 15 pt DemiBold, text darker(130) |

Startup menu bar (macOS): File → "Import PDF..." ⌘⇧I, "Import Folder...",
"Open Project..." ⌘O, "Quit"; Help → "Install Zotero Plugin…", "About
ScanTailor Spectre". About shows `version 2.0b2 (YYYYMMDD.HHMM)`.

## 2. Main window

`src/app/MainWindow.ui` + `MainWindow.cpp`. Default `resize(800, 550)`. No
toolbars. Chrome = menu bar, two docks, status bar. Central widget = one
`QFrame#imageViewFrame` (image well `#c8c8c8`).

| Dock | Title | Area | Min size |
|---|---|---|---|
| `filterDockWidget` | "Stages" | Left | 274 × 204 |
| `thumbnailsDockWidget` | "Thumbnails" | Right | — |

Left dock = `StageListView` (10 numbered rows, hidden header, square
right-hand cell holding the 18×18 `play` button on the selected row; animated
bubbles there during batch) over a `QScrollArea` holding the active stage's
OptionsWidget. Both docks hide during batch processing.

Batch overlay replaces the image: 24 pt bold "p X / Y", a 128×128 stop
button, `SystemLoadWidget`, checkbox "Beep when finished".

Right dock = 22×22 tool rail (16 px icons) + `thumbView` (min width 150,
multi-column by default, logical thumb 250×160, zoom clamp 100–1000 ×
64–640) + hidden-by-default `sortOptions` combo. Rail, top to bottom: eye
(keep in view, checked), prev/next, check-mark (selected only), magnify,
diminish, go-to-page, multi-select mode, single/multi column, sort order,
deviation highlight, then text buttons "B" "G" "M" "C" (show/hide by color
mode, all checked).

Status bar: timing label (stretch) + `StatusBarPanel`: zone mode icon,
mouse position, image size, "page N / M".

Auto mode: Tools → "Auto Process…" (Meta+Shift+A/O = Control on macOS,
README says Cmd) opens AutoProcessDialog then walks stages 2→8.

Export stage pins a footer (Zotero group + "Export PDF...") under an inner
scroller; MainWindow grows the dock min width for it.

## 3. Stages (StageSequence order, `Filter::getName()`)

1 "Fix Orientation" · 2 "Split Pages" · 3 "Deskew" · 4 "Page Box" ·
5 "Select Content" · 6 "Margins" · 7 "Finalize" · 8 "Output" · 9 "OCR" ·
10 "Export".

Page orderings offered: Split Pages: Natural / by split type. Deskew:
Natural / decreasing deviation. Page Box, Select Content, Margins: Natural /
increasing width / increasing height / decreasing deviation. Output: Natural
/ by completeness. Others: Natural only (Fix Orientation: none).

Bare-key shortcuts: O E N V K F switch stages 1,2,3,5,6,8; c/g/b set color
mode; p toggles pass-through; Shift+C/G/M/B toggle thumbnail filters; Q/W
and PgUp/PgDn move pages; Home/End; Ctrl+G go to page; F5 reload.

## 4. Per-stage options widgets (top to bottom)

Selection indicator (7 of 10 stages): "Editing %1 pages", `color:#666;
font-weight:500` (Page Box: "%1 pages selected", unstyled).

- **Fix Orientation**: group "Rotate": rotate-left / rotate-right tool
  buttons, rotation indicator, "Reset"; indicator; "Apply to ...".
- **Split Pages**: group "Page Layout": three layout tool buttons (single
  uncut / page+offcut / two pages), "?" scope label, indicator, "Change ...";
  group "Split Line": "Auto" | "Manual"; "Reset All Splits".
- **Deskew**: group "Deskew": "Auto" | "Manual", angle spinbox (°),
  indicator, "Apply To ...".
- **Page Box**: indicator; group "Page Box": "Disable" | "Auto" | "Manual";
  group "Options": "Fine Tune Page Corners", Width / Height spinboxes;
  "Apply to ...".
- **Select Content**: group "Content Box": "Auto" | "Disable" | "Manual";
  group "Detection Settings": "Fill Factor" slider 50–100 shown 0.50–1.00
  (default 0.65), "Border Tolerance" slider 0–20 px (default 2); indicator;
  "Apply to ...".
- **Margins**: indicator; group "Margins": "Auto Margins", vchain link
  buttons, Top/Bottom/Left/Right spinboxes, "Apply To ..."; group
  "Alignment": "Match size with other pages", Horizontal mode / Vertical
  mode combos (Auto, Manual, Original), 3×3 alignment grid, "Apply To ...";
  group "Guides Help": text.
- **Finalize**: group "Color Mode": combo (Black and White, Grayscale,
  Mixed, Color), status "Not yet processed", "Auto White Balance",
  "Picture detection sensitivity:" spin 1–20 (default 8), indicator,
  "Apply To ..."; group "Detection Cache": "Clear This Page", "Clear All
  Pages"; group "Output Images": Format (TIFF, PNG, JPEG), Compression (LZW,
  Deflate), Quality slider 1–100 (default 90).
- **Output**: indicator; "Output Resolution": dpi + "Change ..."; "Mode":
  combo (Black and White, Color, Grayscale, Mixed), "Apply To..."; "Options":
  Pass-through, Fill offcut, Fill margins, White | Background, Equalize
  illumination (B&W), Savitzky-Golay smoothing, Morphological smoothing;
  "Threshold": Method combo (21 methods: Otsu, Sauvola, Wolf, Bradley, Grad,
  EdgePlus, BlurDiv, EdgeDiv, Niblack, N.I.C.K, Singh, WAN, MultiScale,
  Robust, Gatos, Window, Fox, Engraving, BiModal, Mean, Grain) + per-method
  options; "Reduce Noise": Wiener coef/window, Color segmentation R/G/B +
  Reduce noise, Posterize level/Normalize/Force b&w; "Picture Shape": Off /
  Free / Rectangular, Sensitivity %, Higher search sensitivity; "Photo
  Adjustments": Auto | Reset, Temp, Tint, Exposure, Contrast, Highlights,
  Shadows, Whites, Blacks (centered sliders −100..100); "Splitting": Split
  output, B&W foreground | Color foreground, Original background, "Apply To
  ..."; "Despeckling": Despeckle, slider 10–30, "Apply To ..."; "Depth
  perception": slider 10–30, "Apply To ..."; "Dewarping": status + "Change
  ...".
- **OCR**: group "Text Recognition (OCR)": "Enable OCR for searchable PDF",
  "Primary language:" combo (Apple Vision codes), "Fast OCR", "Multilingual
  detection (slower)", "Skip language correction (faster)"; group "Current
  Page": "Status: Not processed", "Text blocks: 0", "Clear Result", "Clear
  All".
- **Export** (scroller + pinned footer): "Output Resolution": "300 dpi" +
  "Change ..."; "PDF Export": "No DPI limit (use Output resolution)", "Max
  Image DPI:" 72–1200 (default 400), "Compress grayscale (JPEG)", "JPEG
  Quality:" High (95%) / Medium (85%) / Low (70%); "Book Metadata": Title,
  Author(s), Role (Author(s)/Editor(s)/Translator(s)), Year, Publisher,
  Place, ISBN, Language, "Guess from scan", "Look up ISBN", "Suggest file
  name from metadata". Footer: group "Zotero": warning label (`#d17a00`),
  status "Zotero: checking…" / "Zotero round-trip project" / "Zotero:
  running" / "Zotero: not running", checkbox "Send to Zotero after export";
  button "Export PDF...".

## 5. Dialogs

- **Project Files** (new project): Input Directory, Output Directory, two
  facing lists "Files Not In Project" / "Files In Project" with transfer
  arrows, "Right to left layout (for Hebrew and Arabic)", "Fix DPIs, even if
  they look OK", progress bar, OK/Cancel.
- **Apply to** (5 stages): radios "This page only (already applied)", "All
  pages", "This page and the following ones", "This page and the following
  every other page", "Every other page", "Selected pages", "Every other
  selected page". Select Content adds "Apply page box" / "Apply content box".
  Output colors dialog has only the first, second, third and "Selected
  pages".
- **Batch summaries** (same shape: summary → threshold slider → two view
  toggles → "Double-click a page to jump to it:" list → Jump / action /
  action-all / Close): "Batch Processing Complete" (Split: Not Split (n) /
  Split (n); Force Two-Page / Force Single Page), "Page Box Detection
  Complete" (Outlier / Normal, deviation 10%), "Content Detection Complete"
  (Low / Normal coverage, threshold 50%; Preserve Layout), "Page Size Issue"
  (Outlier pages, sensitivity 30%; Detach Selected / Detach All Listed / Go
  to Page Split).
- **Auto Process**: "Color handling": Force black & white / Black & white +
  grayscale / Best guess (default); "Re-detect color for all pages"; "Run
  OCR after Output"; "OCR options": Fast OCR, Multilingual detection
  (slower), Skip language correction (faster).
- **Settings**: tabs General (User Interface: OpenGL, Device, Auto-save,
  Color Scheme, Language; Thumbnails: Quality, Size, Single column, Show
  question on canceling multi page selection) and Processing (White on black
  detection; Deviation highlighting + per-stage coef/threshold).
- Page context menu: Insert before / after, Remove from project, Force
  two-page / single page, Reset to auto layout, Process from here...,
  Convert to Black and White / Grayscale / Color (Finalize, Output).

## 6. Menus

File: New Project ⌘N, Open Project ⌘O, Import PDF ⌘I, Save ⌘S, Duplicate
Project..., Export to PDF ⌘E, Close ⌘W, Quit ⌘Q. Tools: Auto Process…, Fix
DPI, Relinking, Remove Selected Pages ⌫, Settings, Default parameters, Units
(Pixels / Millimetres / Centimetres / Inches). Help: Install Zotero Plugin…,
About. No Edit or View menu.

## 7. Visual vocabulary

Fusion style always (all three schemes). System font, no bundled fonts.
Square corners everywhere (`border-radius: 0`). QSS px values are
em-converted against a 16 px base.

| Role | Light | Dark |
|---|---|---|
| Window | #F0F0F0 | #535353 |
| WindowText | #303030 | #DDDDDD |
| WindowText disabled | #909090 | #989898 |
| Base | #FCFCFC | #454545 |
| Mid | #CCCCCC | #333333 |
| Dark | #DADADA | #404040 |
| Highlight | #B5B5B5 | #6B6B6B |
| Link | #0000FF | #4F95FC |
| BrightText | #F40000 | #FC5248 |
| Image well | #c8c8c8 | — |
| Dock title bg / border | #e8e8e8 / #ccc | — |
| Button | border 1px #ccc, transparent, padding 2px 10px, min-h 18, font 11px, color #555 | — |
| Thumb selected bg / leader | #727272 / #5E5E5E | — |
| Zotero warning | #d17a00 | — |

There is no accent color. Icons: 68 SVGs at mixed grids (mostly 16, some
24/32), recolored per state by `StyledIconPack`. App icon:
`src/resources/scantailor-spectre2.png` 1024².

## 8. Known inconsistencies (decide, don't port)

- Auto Process shortcut is Control-based on macOS; README says Cmd.
- Import PDF: ⌘⇧I on startup, ⌘I in main.
- Six stage-switch actions share the label "Switch filter to orientation".
- "Apply to ..." / "Apply To ..." / "Apply To..." spelling drifts; Margins
  has two, Output four.
- `palette(alternative-base)` is an invalid QSS role and is dropped.
- Native scheme in macOS Dark Mode applies no stylesheet at all.
