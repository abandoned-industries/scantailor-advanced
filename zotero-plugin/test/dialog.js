"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

class Element {
	constructor(id = "") {
		this.id = id;
		this.textContent = "";
		this.title = "";
		this.hidden = false;
		this.disabled = false;
		this.selectedIndex = -1;
		this.children = [];
		this.listeners = new Map();
	}

	appendChild(child) {
		this.children.push(child);
		if (this.selectedIndex < 0) {
			this.selectedIndex = 0;
		}
		return child;
	}

	addEventListener(type, listener) {
		this.listeners.set(type, listener);
	}
}

function createHarness(model) {
	const ids = [
		"item-title", "source-label", "source-picker", "workspace-path",
		"change-workspace", "error-message", "cancel", "clean",
	];
	const elements = Object.fromEntries(ids.map((id) => [id, new Element(id)]));
	elements["source-picker"].hidden = true;
	elements["clean"].disabled = true;
	let closeCount = 0;
	const window = {
		arguments: [model],
		browsingContext: {},
		close() { closeCount++; },
	};
	const context = vm.createContext({
		console,
		window,
		document: {
			getElementById: (id) => elements[id],
			createElementNS(namespace, tag) {
				assert.equal(namespace, "http://www.w3.org/1999/xhtml");
				assert.equal(tag, "option");
				return new Element();
			},
		},
	});
	const source = fs.readFileSync(path.join(__dirname, "..", "content", "dialog.js"), "utf8");
	vm.runInContext(source, context, { filename: "dialog.js" });
	return {
		dialog: context.STSpectreLoopDialog,
		elements,
		closeCount: () => closeCount,
		window,
	};
}

(async () => {
	const chosenRoots = [];
	const validatedRoots = [];
	const controller = {
		async chooseWorkspaceRoot(dialogWindow, currentRoot) {
			assert.ok(dialogWindow.browsingContext);
			chosenRoots.push(currentRoot);
			return { path: "/Volumes/Projects/ScanTailor Loop" };
		},
		async validateWorkspaceRoot(root) {
			validatedRoots.push(root);
			return root;
		},
	};

	const singleModel = {
		accepted: false,
		itemTitle: "A long Zotero item title",
		candidates: [{ label: "scan.pdf", path: "/source/scan.pdf" }],
		selectedIndex: 0,
		workspaceRoot: "",
		workspaceError: "Choose a projects folder.",
		controller,
	};
	let harness = createHarness(singleModel);
	harness.dialog.init();
	assert.equal(harness.elements["item-title"].textContent, singleModel.itemTitle);
	assert.equal(harness.elements["item-title"].title, singleModel.itemTitle);
	assert.equal(harness.elements["source-label"].textContent, "scan.pdf");
	assert.equal(harness.elements["source-label"].title, "/source/scan.pdf");
	assert.equal(harness.elements["source-picker"].hidden, true);
	assert.equal(harness.elements.clean.disabled, true);
	assert.equal(harness.elements["error-message"].hidden, false);
	assert.ok(harness.elements["change-workspace"].listeners.has("click"));
	assert.ok(harness.elements.cancel.listeners.has("click"));
	assert.ok(harness.elements.clean.listeners.has("click"));

	await harness.dialog.changeWorkspace();
	assert.deepEqual(chosenRoots, [""]);
	assert.equal(singleModel.workspaceRoot, "/Volumes/Projects/ScanTailor Loop");
	assert.equal(harness.elements["workspace-path"].textContent, singleModel.workspaceRoot);
	assert.equal(harness.elements.clean.disabled, false);
	assert.equal(harness.elements["error-message"].hidden, true);
	await harness.dialog.confirm();
	assert.deepEqual(validatedRoots, [singleModel.workspaceRoot]);
	assert.equal(singleModel.accepted, true);
	assert.equal(singleModel.selectedIndex, 0);
	assert.equal(harness.closeCount(), 1);
	console.log("dialog controller: single PDF, folder choice, validation, and acceptance pass");

	const multipleModel = {
		accepted: false,
		itemTitle: "Two scans",
		candidates: [
			{ label: "first.pdf", path: "/source/first.pdf" },
			{ label: "second.pdf", path: "/source/second.pdf" },
		],
		selectedIndex: 1,
		workspaceRoot: "/tmp/projects",
		workspaceError: "",
		controller,
	};
	harness = createHarness(multipleModel);
	harness.dialog.init();
	assert.equal(harness.elements["source-label"].hidden, true);
	assert.equal(harness.elements["source-picker"].hidden, false);
	assert.equal(harness.elements["source-picker"].children.length, 2);
	assert.equal(harness.elements["source-picker"].children[1].textContent, "second.pdf");
	assert.equal(harness.elements["source-picker"].children[1].title, "/source/second.pdf");
	assert.equal(harness.elements["source-picker"].selectedIndex, 1);
	await harness.dialog.confirm();
	assert.equal(multipleModel.selectedIndex, 1);
	assert.equal(multipleModel.accepted, true);
	console.log("dialog controller: multiple-PDF selection pass");

	console.log("DIALOG PASS");
})().catch((err) => {
	console.error(err);
	process.exitCode = 1;
});
