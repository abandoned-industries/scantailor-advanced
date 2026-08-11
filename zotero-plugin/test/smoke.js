"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const manifest = JSON.parse(fs.readFileSync(path.join(__dirname, "..", "manifest.json"), "utf8"));

const preferences = new Map();
const existingFiles = new Set([
	"/tmp/cleaned.pdf",
	"/tmp/one.pdf",
	"/tmp/two-a.pdf",
	"/tmp/two-b.pdf",
	"/tmp/standalone.pdf",
	"/Applications/ScanTailor Spectre.app",
]);
const directories = new Set(["/tmp/loop-workspace", "/tmp/other-loop-workspace"]);
const unwritable = new Set();
const copies = [];
const writes = [];
const launches = [];
const imports = [];
const indexCalls = [];
const pickerSelections = [];
const pickerCalls = [];

function regular(id, key, title, attachmentIDs = []) {
	return {
		id,
		key,
		libraryID: 1,
		deleted: false,
		isRegularItem: () => true,
		isAttachment: () => false,
		getField: (field) => field === "title" ? title : "",
		getAttachments: () => attachmentIDs,
		getBestAttachments: async () => attachmentIDs,
	};
}

function pdf(id, key, parentItemID, filePath, title) {
	return {
		id,
		key,
		parentItemID,
		deleted: false,
		attachmentContentType: "application/pdf",
		isRegularItem: () => false,
		isAttachment: () => true,
		isPDFAttachment: () => true,
		getFilePathAsync: async () => filePath,
		getField: (field) => field === "title" ? title : "",
	};
}

const oneParent = regular(42, "ABCD1234", "One PDF", [101]);
const multiParent = regular(43, "EFGH5678", "Two PDFs", [102, 103]);
const emptyParent = regular(44, "IJKL9012", "No PDFs", []);
const onePdf = pdf(101, "PDFONE01", 42, "/tmp/one.pdf", "Volume One");
const twoPdfA = pdf(102, "PDFA0002", 43, "/tmp/two-a.pdf", "First scan");
const twoPdfB = pdf(103, "PDFB0003", 43, "/tmp/two-b.pdf", "Second scan");
const standalonePdf = pdf(104, "PDFNONE4", null, "/tmp/standalone.pdf", "Standalone");
const items = new Map([
	[42, oneParent], [43, multiParent], [44, emptyParent],
	[101, onePdf], [102, twoPdfA], [103, twoPdfB], [104, standalonePdf],
]);

let selectedItems = [];

class StrictFilePicker {
	constructor() {
		this.modeGetFolder = 2;
		this.returnCancel = 1;
		this.file = "";
		this.displayDirectory = "";
	}

	init(parentWindow, title, mode) {
		assert.ok(parentWindow?.browsingContext, "FilePicker must receive a window with a browsingContext");
		assert.equal(mode, this.modeGetFolder);
		pickerCalls.push({ parentWindow, title, mode, picker: this });
	}

	async show() {
		const selection = pickerSelections.shift();
		if (!selection) {
			return this.returnCancel;
		}
		this.file = selection;
		return 0;
	}
}

const context = vm.createContext({
	console,
	addonVersion: manifest.version,
	setTimeout,
	clearTimeout,
	IOUtils: {
		exists: async (filePath) => existingFiles.has(filePath) || directories.has(filePath),
		writeUTF8: async (filePath, contents) => {
			if (unwritable.has(path.dirname(filePath))) {
				throw new Error("permission denied");
			}
			writes.push({ filePath, contents });
			existingFiles.add(filePath);
		},
		remove: async (filePath) => existingFiles.delete(filePath),
		makeDirectory: async (filePath) => directories.add(filePath),
		copy: async (source, destination) => {
			copies.push({ source, destination });
			existingFiles.add(destination);
		},
	},
	PathUtils: { join: path.join },
	ChromeUtils: {
		importESModule(uri) {
			assert.equal(uri, "chrome://zotero/content/modules/filePicker.mjs");
			return { FilePicker: StrictFilePicker };
		},
	},
	Services: { prompt: { alert: () => {} } },
	Zotero: {
		Prefs: {
			get: (key) => preferences.get(key),
			set: (key, value) => preferences.set(key, value),
		},
		Utilities: {
			randomString: () => "A".repeat(32),
			Internal: { exec: async (command, args) => launches.push({ command, args }) },
		},
		Server: { Endpoints: {} },
		File: {
			pathToFile: (filePath) => ({
				path: filePath,
				exists: () => existingFiles.has(filePath) || directories.has(filePath),
				isFile: () => existingFiles.has(filePath),
				isReadable: () => existingFiles.has(filePath),
			}),
		},
		Items: {
			get: (id) => items.get(id) || null,
			getAsync: async (id) => items.get(id) || null,
			getByLibraryAndKey: (libraryID, itemKey) => (
				libraryID === 1 && itemKey === "ABCD1234" ? oneParent : null
			),
		},
		Attachments: {
			importFromFile: async (options) => {
				imports.push(options);
				return { id: 84, key: "WXYZ5678" };
			},
		},
		Fulltext: { indexItems: async (...args) => indexCalls.push(args) },
		debug: () => {},
	},
});

const pluginPath = path.join(__dirname, "..", "content", "scripts", "st-spectre-loop.js");
vm.runInContext(fs.readFileSync(pluginPath, "utf8"), context, { filename: pluginPath });
const loop = context.STSpectreLoop;
const win = {
	browsingContext: {},
	ZoteroPane: { getSelectedItems: () => selectedItems },
};

async function rejectionMessage(promise) {
	try {
		await promise;
	}
	catch (err) {
		return err.message;
	}
	assert.fail("expected rejection");
}

(async () => {
	assert.equal(loop.slugify("  Étude: The Book!  "), "etude-the-book");
	assert.equal(loop.slugify("***"), "untitled");

	selectedItems = [onePdf];
	let selection = await loop.resolveSelectedRow(win);
	assert.equal(selection.item, oneParent);
	assert.equal(selection.candidates.length, 1);
	assert.equal(selection.candidates[0].attachment, onePdf);
	console.log("selection: selected PDF is used exactly and resolves its live parent");

	selectedItems = [oneParent];
	selection = await loop.resolveSelectedRow(win);
	assert.equal(selection.candidates.length, 1);
	assert.equal(selection.candidates[0].path, "/tmp/one.pdf");

	selectedItems = [multiParent];
	selection = await loop.resolveSelectedRow(win);
	assert.deepEqual(Array.from(selection.candidates, (entry) => entry.path), [
		"/tmp/two-a.pdf", "/tmp/two-b.pdf",
	]);
	console.log("selection: regular parent exposes one or all readable PDFs for the dialog");

	selectedItems = [emptyParent];
	assert.match(await rejectionMessage(loop.resolveSelectedRow(win)), /no readable local PDF/i);
	selectedItems = [standalonePdf];
	assert.match(await rejectionMessage(loop.resolveSelectedRow(win)), /no parent Zotero item/i);
	selectedItems = [oneParent, multiParent];
	assert.match(await rejectionMessage(loop.resolveSelectedRow(win)), /exactly one/i);
	console.log("selection: zero PDFs, standalone PDFs, and multi-row selection fail clearly");

	preferences.set(loop.PREF_WORKSPACE, "/tmp/loop-workspace");
	pickerSelections.push(null);
	assert.deepEqual(
		JSON.parse(JSON.stringify(await loop.chooseWorkspaceRoot(win, "/tmp/loop-workspace"))),
		{ cancelled: true }
	);
	assert.equal(preferences.get(loop.PREF_WORKSPACE), "/tmp/loop-workspace");
	assert.equal(pickerCalls[0].picker.displayDirectory, "/tmp/loop-workspace");

	unwritable.add("/tmp/other-loop-workspace");
	pickerSelections.push("/tmp/other-loop-workspace");
	const invalidChoice = await loop.chooseWorkspaceRoot(win, "/tmp/loop-workspace");
	assert.match(invalidChoice.error, /cannot write/i);
	assert.equal(preferences.get(loop.PREF_WORKSPACE), "/tmp/loop-workspace");
	unwritable.delete("/tmp/other-loop-workspace");

	pickerSelections.push("/tmp/other-loop-workspace");
	assert.equal(
		(await loop.chooseWorkspaceRoot(win, "/tmp/loop-workspace")).path,
		"/tmp/other-loop-workspace"
	);
	assert.equal(preferences.get(loop.PREF_WORKSPACE), "/tmp/other-loop-workspace");
	console.log("picker: official wrapper contract, string paths, cancel preservation, and atomic preference update");

	selectedItems = [multiParent];
	loop.showConfirmationDialog = async () => null;
	const mutationCounts = [copies.length, writes.length, launches.length];
	assert.equal(await loop.stageSelectedItem(win), null);
	assert.deepEqual([copies.length, writes.length, launches.length], mutationCounts);
	console.log("confirmation: cancel creates, copies, records, and launches nothing");

	loop.showConfirmationDialog = async () => ({
		workspaceRoot: "/tmp/missing-workspace",
		selectedIndex: 0,
	});
	assert.match(await rejectionMessage(loop.stageSelectedItem(win)), /no longer exists/i);
	assert.deepEqual([copies.length, writes.length, launches.length], mutationCounts);
	console.log("confirmation: workspace is revalidated before any staging mutation");

	loop.showConfirmationDialog = async () => ({
		workspaceRoot: "/tmp/loop-workspace",
		selectedIndex: 1,
	});
	const staged = await loop.stageSelectedItem(win);
	assert.equal(copies.at(-1).source, "/tmp/two-b.pdf");
	assert.equal(staged.sidecar.version, 1);
	assert.equal(staged.sidecar.pluginVersion, manifest.version);
	assert.equal(staged.sidecar.itemKey, "EFGH5678");
	assert.equal(staged.sidecar.libraryID, 1);
	assert.equal(JSON.parse(preferences.get(loop.PREF_JOBS)).EFGH5678.sourceBasename, "two-b.pdf");
	assert.equal(launches.at(-2).command, "/usr/bin/open");
	console.log("confirmation: chosen PDF stages only after confirmation and preserves sidecar v1");

	loop.token = "A".repeat(32);
	loop.jobs.ABCD1234 = { libraryID: 1, sourceBasename: "one.pdf" };
	const unauthorized = await loop.handleReturn({
		headers: { authorization: "Bearer wrong" },
		data: { itemKey: "ABCD1234", filePath: "/tmp/cleaned.pdf" },
	});
	assert.equal(unauthorized[0], 401);
	const success = await loop.handleReturn({
		headers: { authorization: `Bearer ${loop.token}` },
		data: { itemKey: "ABCD1234", filePath: "/tmp/cleaned.pdf" },
	});
	assert.equal(success[0], 200);
	assert.equal(imports.at(-1).parentItemID, 42);
	assert.equal(indexCalls.length, 1);
	console.log("return: authenticated cleaned PDF attaches to the recorded regular parent");

	const dialogMarkup = fs.readFileSync(path.join(__dirname, "..", "content", "dialog.xhtml"), "utf8");
	assert.match(dialogMarkup, /Source PDF:/);
	assert.match(dialogMarkup, /Projects folder:/);
	assert.match(dialogMarkup, /<html:button id="change-workspace"[^>]*>Change…<\/html:button>/);
	assert.match(dialogMarkup, /<html:button id="clean"[^>]*disabled="disabled">Clean Scan<\/html:button>/);
	assert.doesNotMatch(dialogMarkup, /<(?:grid|rows|row|columns|column|deck)\b/);
	assert.match(dialogMarkup, /grid-template-columns:\s*118px minmax\(0, 1fr\)/);
	const pluginSource = fs.readFileSync(pluginPath, "utf8");
	assert.match(pluginSource, /Clean scan in ScanTailor Spectre…/);
	assert.doesNotMatch(pluginSource, /nsIFilePicker|choose-workspace/);
	console.log("dialog: one confirmation surface contains PDF choice, folder change, Cancel, and gated Clean Scan");

	console.log("SMOKE PASS");
})().catch((err) => {
	console.error(err);
	process.exitCode = 1;
});
