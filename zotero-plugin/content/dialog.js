"use strict";

var STSpectreLoopDialog = {
	model: null,
	busy: false,

	init() {
		this.model = window.arguments?.[0];
		if (!this.model || !this.model.controller) {
			window.close();
			return;
		}

		const itemTitle = document.getElementById("item-title");
		itemTitle.textContent = this.model.itemTitle;
		itemTitle.title = this.model.itemTitle;
		const candidates = this.model.candidates || [];
		const sourceLabel = document.getElementById("source-label");
		const sourcePicker = document.getElementById("source-picker");
		if (candidates.length > 1) {
			sourceLabel.hidden = true;
			sourcePicker.hidden = false;
			for (const candidate of candidates) {
				const option = document.createElementNS("http://www.w3.org/1999/xhtml", "option");
				option.textContent = candidate.label;
				option.title = candidate.path;
				sourcePicker.appendChild(option);
			}
			sourcePicker.selectedIndex = this.model.selectedIndex || 0;
		}
		else {
			const candidate = candidates[0];
			sourceLabel.textContent = candidate?.label || "Not chosen";
			sourceLabel.title = candidate?.path || "";
		}

		document.getElementById("change-workspace").addEventListener("click", () => this.changeWorkspace());
		document.getElementById("cancel").addEventListener("click", () => window.close());
		document.getElementById("clean").addEventListener("click", () => this.confirm());
		this.setWorkspace(this.model.workspaceRoot || "");
		this.showError(this.model.workspaceError || "");
	},

	setWorkspace(root) {
		this.model.workspaceRoot = root;
		const label = document.getElementById("workspace-path");
		label.textContent = root || "Not chosen";
		label.title = root || "";
		document.getElementById("clean").disabled = !root || this.busy;
	},

	showError(message) {
		const error = document.getElementById("error-message");
		error.textContent = message || "";
		error.hidden = !message;
	},

	async changeWorkspace() {
		if (this.busy) {
			return;
		}
		this.busy = true;
		document.getElementById("change-workspace").disabled = true;
		document.getElementById("clean").disabled = true;
		try {
			const result = await this.model.controller.chooseWorkspaceRoot(
				window,
				this.model.workspaceRoot || ""
			);
			if (result?.path) {
				this.setWorkspace(result.path);
				this.showError("");
			}
			else if (result?.error) {
				this.showError(result.error);
			}
		}
		catch (err) {
			this.showError(err instanceof Error ? err.message : String(err));
		}
		finally {
			this.busy = false;
			document.getElementById("change-workspace").disabled = false;
			document.getElementById("clean").disabled = !this.model.workspaceRoot;
		}
	},

	async confirm() {
		if (this.busy || !this.model.workspaceRoot) {
			return;
		}
		this.busy = true;
		document.getElementById("clean").disabled = true;
		document.getElementById("change-workspace").disabled = true;
		this.showError("");
		try {
			await this.model.controller.validateWorkspaceRoot(this.model.workspaceRoot);
			const picker = document.getElementById("source-picker");
			this.model.selectedIndex = this.model.candidates.length > 1
				? picker.selectedIndex
				: 0;
			if (this.model.selectedIndex < 0) {
				throw new Error("Choose a source PDF.");
			}
			this.model.accepted = true;
			window.close();
		}
		catch (err) {
			this.showError(err instanceof Error ? err.message : String(err));
			this.busy = false;
			document.getElementById("change-workspace").disabled = false;
			document.getElementById("clean").disabled = !this.model.workspaceRoot;
		}
	},

	cancel() {
		if (this.model && !this.model.accepted) {
			this.model.accepted = false;
		}
		return true;
	},
};
