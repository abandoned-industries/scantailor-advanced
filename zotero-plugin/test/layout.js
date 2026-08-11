"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

let firefox;
try {
	({ firefox } = require("playwright"));
}
catch (err) {
	throw new Error(
		"The dialog layout test requires Playwright. Run zotero-plugin/build.sh with "
		+ "the bundled workspace runtime or install Playwright locally.\n"
		+ err.message
	);
}

const WIDTH = 620;
const HEIGHT = 330;
const SCREENSHOT = process.env.ST_SPECTRE_LAYOUT_SCREENSHOT
	|| "/private/tmp/st-spectre-loop-dialog-layout.png";
const dialogPath = path.resolve(__dirname, "..", "content", "dialog.xhtml");
const executablePath = process.env.PLAYWRIGHT_FIREFOX_EXECUTABLE_PATH || undefined;

assert.ok(fs.existsSync(dialogPath), `dialog not found: ${dialogPath}`);
if (executablePath) {
	assert.ok(fs.existsSync(executablePath), `Firefox renderer not found: ${executablePath}`);
}

function renderDocument() {
	const source = fs.readFileSync(dialogPath, "utf8");
	const styleMatch = source.match(/<html:style>([\s\S]*?)<\/html:style>/);
	const mainMatch = source.match(/(<html:main\b[\s\S]*?<\/html:main>)/);
	assert.ok(styleMatch, "dialog XHTML must contain one inline HTML style block");
	assert.ok(mainMatch, "dialog XHTML must contain one HTML main element");
	const main = mainMatch[1].replace(/(<\/?)(?:html:)/g, "$1");
	return `<!doctype html>
		<html><head><meta charset="utf-8"><style>
			html, body { width: 100%; height: 100%; margin: 0; overflow: hidden; }
			${styleMatch[1]}
		</style></head><body>${main}</body></html>`;
}

async function measure(page) {
	return page.evaluate(() => {
		const ids = [
			"item-title", "source-field", "source-label", "source-picker", "workspace-path",
			"change-workspace", "error-message", "cancel", "clean",
		];
		const rect = (element) => {
			const box = element.getBoundingClientRect();
			return {
				left: box.left, top: box.top, right: box.right, bottom: box.bottom,
				width: box.width, height: box.height,
			};
		};
		const result = Object.fromEntries(ids.map((id) => [id, rect(document.getElementById(id))]));
		result.rows = Array.from(document.querySelectorAll(".details > .field-label"), rect);
		result.shell = rect(document.querySelector(".dialog-shell"));
		result.viewport = { width: innerWidth, height: innerHeight };
		result.scroll = {
			documentWidth: document.documentElement.scrollWidth,
			bodyWidth: document.body?.scrollWidth || 0,
		};
		result.truncated = {
			item: document.getElementById("item-title").scrollWidth
				> document.getElementById("item-title").clientWidth,
			workspace: document.getElementById("workspace-path").scrollWidth
				> document.getElementById("workspace-path").clientWidth,
		};
		return result;
	});
}

function assertInside(rect, viewport, name) {
	assert.ok(rect.width > 0 && rect.height > 0, `${name} must be visible`);
	assert.ok(rect.left >= 0 && rect.top >= 0, `${name} starts outside viewport`);
	assert.ok(rect.right <= viewport.width + 0.5, `${name} exceeds viewport width`);
	assert.ok(rect.bottom <= viewport.height + 0.5, `${name} exceeds viewport height`);
}

function assertCommonLayout(layout) {
	assert.deepEqual(layout.viewport, { width: WIDTH, height: HEIGHT });
	for (const [name, rect] of Object.entries(layout)) {
		if (!["rows", "shell", "viewport", "scroll", "truncated", "source-label", "source-picker"].includes(name)) {
			assertInside(rect, layout.viewport, name);
		}
	}
	assertInside(layout.shell, layout.viewport, "dialog shell");
	assert.equal(layout.rows.length, 3, "dialog must have three field rows");
	assert.ok(layout.rows[0].top < layout.rows[1].top && layout.rows[1].top < layout.rows[2].top,
		"field rows must be vertically ordered");
	assert.ok(layout.rows.every((row) => Math.abs(row.left - layout.rows[0].left) < 1),
		"field labels must share a left edge");
	assert.ok(Math.abs(layout["item-title"].left - layout["source-field"].left) < 1,
		"field values must share a left edge");
	assert.ok(layout["change-workspace"].left > layout["workspace-path"].left,
		"Change button must remain visible to the right of the workspace path");
	assert.ok(layout.cancel.top > HEIGHT - 70 && layout.clean.top > HEIGHT - 70,
		"action buttons must stay at the bottom of the dialog");
	assert.ok(layout.clean.left > layout.cancel.left, "Clean Scan must follow Cancel");
	assert.ok(layout["error-message"].bottom < layout.cancel.top,
		"inline error must not overlap action buttons");
	assert.ok(layout.scroll.documentWidth <= WIDTH && layout.scroll.bodyWidth <= WIDTH,
		"dialog must not overflow horizontally");
	assert.equal(layout.truncated.item, true, "long item title must truncate");
	assert.equal(layout.truncated.workspace, true, "long workspace path must truncate");
}

async function populate(page) {
	await page.evaluate(() => {
		const put = (id, text) => {
			const element = document.getElementById(id);
			element.textContent = text;
			element.title = text;
		};
		put("item-title", "Literacy and orality in ancient Greece: an intentionally very long bibliographic title with extended publication and archival details");
		put("source-label", "Literacy-and-orality-in-ancient-Greece-complete-preservation-scan.pdf");
		put("workspace-path", "/Users/example/Documents/Archival Projects/ScanTailor Loop/An intentionally long workspace folder");
		const error = document.getElementById("error-message");
		error.textContent = "Choose a writable projects folder before cleaning this scan.";
		error.hidden = false;
	});
}

(async () => {
	let browser;
	try {
		browser = await firefox.launch({ headless: true, executablePath });
	}
	catch (err) {
		throw new Error(
			"Could not launch the isolated Playwright Firefox renderer. On macOS the test "
			+ "must be allowed to launch a local headless renderer.\n"
			+ err.message
		);
	}

	try {
		const context = await browser.newContext({ viewport: { width: WIDTH, height: HEIGHT } });
		const page = await context.newPage();
		// Firefox intentionally refuses to load an unprivileged file:// document whose root
		// uses the privileged XUL namespace. Extract the actual HTML-namespace subtree and
		// its actual CSS into an isolated page; no duplicate layout fixture is maintained.
		await page.setContent(renderDocument(), { waitUntil: "load" });
		await populate(page);

		const singleLayout = await measure(page);
		assertCommonLayout(singleLayout);
		assertInside(singleLayout["source-label"], singleLayout.viewport, "single-PDF source label");
		assert.equal(singleLayout["source-picker"].width, 0,
			"single-PDF state must not render an empty source selector");
		assert.equal(singleLayout["source-picker"].height, 0,
			"single-PDF state must not consume a second row");
		await page.screenshot({ path: SCREENSHOT });

		await page.evaluate(() => {
			const sourceLabel = document.getElementById("source-label");
			const sourcePicker = document.getElementById("source-picker");
			sourceLabel.hidden = true;
			sourcePicker.hidden = false;
			for (const label of ["First preservation scan.pdf", "Second preservation scan.pdf"]) {
				const option = document.createElementNS("http://www.w3.org/1999/xhtml", "option");
				option.textContent = label;
				sourcePicker.appendChild(option);
			}
			sourcePicker.selectedIndex = 1;
		});
		const multipleLayout = await measure(page);
		assertCommonLayout(multipleLayout);
		assertInside(multipleLayout["source-picker"], multipleLayout.viewport, "multiple-PDF selector");
		assert.equal(multipleLayout["source-label"].width, 0,
			"multiple-PDF state must hide the single source label");
		assert.equal(await page.locator("#source-picker option").count(), 2,
			"multiple-PDF state must render every choice");
		assert.equal(await page.locator("#source-picker").inputValue(), "Second preservation scan.pdf");

		await context.close();
		console.log(`LAYOUT PASS Firefox/Gecko ${WIDTH}x${HEIGHT}; screenshot: ${SCREENSHOT}`);
	}
	finally {
		await browser.close();
	}
})().catch((err) => {
	console.error(err);
	process.exitCode = 1;
});
