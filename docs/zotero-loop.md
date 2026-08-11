# ScanTailor Spectre Zotero round-trip contract

This document is the shared contract between the Zotero add-on (slice 1) and ScanTailor Spectre (slice 2). The add-on stages a source PDF and remembers its original Zotero parent. ScanTailor Spectre later returns a cleaned PDF to that parent through Zotero's local HTTP server.

## Staging directory

The add-on contributes one item-context command: **Clean scan in ScanTailor
Spectre…**. It opens a confirmation dialog showing the Zotero parent, source
PDF, and current projects folder. **Change…** in that dialog uses Zotero's
supported folder picker. On first use, the folder reads **Not chosen** and
**Clean Scan** remains disabled until a writable folder is selected.

The add-on validates a folder by creating and removing a small probe file, then
remembers it in the Zotero preference `stSpectreLoop.workspacePath`. Cancelling
the picker or choosing an invalid folder leaves the prior preference unchanged.
An invalid choice is explained in the dialog. There is no implicit Documents or
temporary-directory fallback. Work directories live below the chosen folder:

```text
<chosen-workspace>/<item-title-slug>/
```

The Zotero add-on creates a collision-safe directory. If the title slug already exists, it appends `-2`, `-3`, and so on. It copies the chosen source PDF into the new directory; it never edits the Zotero-managed source file.

## Zotero selection

- Selecting a readable PDF attachment uses that exact attachment and returns
  the cleaned file to its live regular parent item.
- Selecting a regular item with one readable PDF selects it automatically.
- Selecting a regular item with several readable PDFs adds a source-PDF chooser
  to the same confirmation dialog.
- A regular item with no readable local PDF, a standalone PDF, or a selection
  containing anything other than exactly one row produces a clear error.

Cancelling the confirmation dialog creates no directory, copy, job record, or
application launch. **Clean Scan** revalidates the workspace before any of those
actions begin.

Each work directory contains `.zotero-loop.json` with this version 1 shape:

```json
{
  "version": 1,
  "pluginVersion": "0.1.2",
  "itemKey": "ABCD1234",
  "libraryID": 1,
  "itemTitle": "Example Book",
  "sourcePdf": "/Users/owner/Chosen Folder/example-book/source.pdf",
  "returnUrl": "http://127.0.0.1:23119/st-spectre/return",
  "token": "persistent-plugin-token"
}
```

- `version` is the integer `1`.
- `pluginVersion` is an informational string carrying the add-on's manifest version.
- `itemKey` and `libraryID` identify the original regular Zotero item.
- `itemTitle` is the original item's display title.
- `sourcePdf` is the absolute path of the staged copy, not the Zotero-managed original.
- `returnUrl` is exactly `http://127.0.0.1:23119/st-spectre/return` for version 1.
- `token` is the add-on's persistent bearer token from the Zotero preference `stSpectreLoop.token`.

Version-1 sidecars may carry additional informational fields. Readers must
ignore fields they do not recognize; no additional field changes the meaning
of an existing version-1 field. Unknown sidecar versions must not be treated as
version 1.

ScanTailor saves a new project as the visible
`<item-title-slug>.ScanTailor` file. It continues to discover and open projects
created by earlier builds as `.ScanTailor` or `project.ScanTailor`.

## Launching the app

The add-on opens the staging directory with an explicit bundle path, preferring
the Zotero preference `stSpectreLoop.appPath` and falling back to
`/Applications/ScanTailor Spectre.app`. Only if neither exists does it fall back
to launching by name. This matters because several bundles can carry the
identifier `com.scantailor.spectre` at once — a superseded copy in
`/Applications`, builds inside agent worktrees — and a name-based launch may
resolve to any of them.

## Returning a cleaned PDF

For Zotero loop projects, ScanTailor Spectre automatically arms **Return to Zotero**; unchecking it is remembered for that project. Legacy project files that merely stored the former default-off value are migrated to automatic return rather than mistaken for a deliberate opt-out.

ScanTailor Spectre sends:

```http
POST /st-spectre/return HTTP/1.1
Host: 127.0.0.1:23119
Authorization: Bearer <token>
Content-Type: application/json

{"itemKey":"ABCD1234","filePath":"/absolute/path/to/cleaned.pdf"}
```

The JSON fields are:

- `itemKey`: the same original item key from the sidecar.
- `filePath`: the absolute path of the readable cleaned PDF.

The endpoint always requires the bearer token. Its responses are:

- `200 application/json` with `{"attachmentKey":"WXYZ5678"}` after the new attachment is saved.
- `401` when the bearer token is missing or incorrect.
- `404` when `itemKey` does not identify a staged, still-existing original regular item.
- `400` when a required field is missing or `filePath` is not an absolute, readable local file.

The returned PDF is **added as a new imported attachment** with `Zotero.Attachments.importFromFile({ file, parentItemID, title })`. It never replaces or modifies an existing attachment. Its attachment title is the staged source PDF basename plus ` (cleaned)`; for example, `source.pdf (cleaned)`. Zotero full-text indexing is explicitly triggered for the new attachment.

## Add-on switch

The add-on has no separate preferences pane. The behavior is hardcoded on while the add-on is enabled: **May attach cleaned scans to entries.** Disabling the add-on removes the context-menu integration and unregisters the return endpoint, which is the effective switch. The endpoint is never available while the add-on is disabled.
