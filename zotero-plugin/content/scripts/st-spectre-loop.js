var STSpectreLoop = {
	ENDPOINT_PATH: "/st-spectre/return",
	RETURN_URL: "http://127.0.0.1:23119/st-spectre/return",
	PREF_TOKEN: "stSpectreLoop.token",
	PREF_JOBS: "stSpectreLoop.jobs",
	PREF_APP: "stSpectreLoop.appPath",
	PREF_WORKSPACE: "stSpectreLoop.workspacePath",
	DEFAULT_APP_PATH: "/Applications/ScanTailor Spectre.app",
	MENU_ID: "st-spectre-loop-clean-scan",
	TOKEN_PATTERN: /^[A-Za-z0-9]{32}$/,
	token: null,
	jobs: Object.create(null),
	windows: new Map(),
	endpointClass: null,
	registerRetryTimer: null,

	hooks: {
		onStartup: async () => {
			Zotero.STSpectreLoop = STSpectreLoop;
			STSpectreLoop.loadJobs();
			STSpectreLoop.ensureToken();
			STSpectreLoop.registerEndpoint();
			const win = Zotero.getMainWindow?.();
			Zotero.debug(`ScanTailor Spectre Loop: startup; main window ${win ? "found" : "not found"}`);
			if (win) {
				STSpectreLoop.patchWindow(win);
			}
		},

		onMainWindowLoad: async (win) => {
			Zotero.debug("ScanTailor Spectre Loop: main window load");
			STSpectreLoop.patchWindow(win);
		},

		onMainWindowUnload: async (win) => {
			STSpectreLoop.unpatchWindow(win);
		},

		onShutdown: async () => {
			STSpectreLoop.unregisterEndpoint();
			for (const win of Array.from(STSpectreLoop.windows.keys())) {
				STSpectreLoop.unpatchWindow(win);
			}
			delete Zotero.STSpectreLoop;
		},
	},

	ensureToken() {
		if (this.token && this.TOKEN_PATTERN.test(this.token)) {
			return this.token;
		}

		let token = null;
		try {
			const stored = Zotero.Prefs.get(this.PREF_TOKEN);
			if (typeof stored === "string" && this.TOKEN_PATTERN.test(stored)) {
				token = stored;
			}
		}
		catch (err) {
			Zotero.debug(`ScanTailor Spectre Loop: could not read token preference: ${err}`);
		}

		if (!token) {
			token = Zotero.Utilities.randomString(32);
			if (!this.TOKEN_PATTERN.test(token)) {
				throw new Error("Zotero did not generate a valid ScanTailor loop token");
			}
			Zotero.Prefs.set(this.PREF_TOKEN, token);
		}

		this.token = token;
		return token;
	},

	loadJobs() {
		this.jobs = Object.create(null);
		let stored = "";
		try {
			stored = Zotero.Prefs.get(this.PREF_JOBS) || "";
		}
		catch (err) {
			Zotero.debug(`ScanTailor Spectre Loop: could not read staged jobs: ${err}`);
			return;
		}
		if (typeof stored !== "string" || !stored) {
			return;
		}

		try {
			const parsed = JSON.parse(stored);
			if (!parsed || typeof parsed !== "object" || Array.isArray(parsed)) {
				return;
			}
			for (const [itemKey, job] of Object.entries(parsed)) {
				if (
					/^[A-Z0-9]{8}$/.test(itemKey)
					&& Number.isInteger(job?.libraryID)
					&& typeof job?.sourceBasename === "string"
					&& job.sourceBasename
				) {
					this.jobs[itemKey] = {
						libraryID: job.libraryID,
						sourceBasename: job.sourceBasename,
					};
				}
			}
		}
		catch (err) {
			Zotero.debug(`ScanTailor Spectre Loop: ignored invalid staged-job preference: ${err}`);
		}
	},

	rememberJob(item, sourceBasename) {
		const itemKey = String(item?.key || "").trim().toUpperCase();
		if (!/^[A-Z0-9]{8}$/.test(itemKey) || !Number.isInteger(item?.libraryID)) {
			throw new Error("The selected Zotero item has no usable library key");
		}
		this.jobs[itemKey] = {
			libraryID: item.libraryID,
			sourceBasename,
		};
		Zotero.Prefs.set(this.PREF_JOBS, JSON.stringify(this.jobs));
	},

	registerEndpoint() {
		if (this.endpointClass) {
			return;
		}
		if (!Zotero.Server?.Endpoints) {
			if (!this.registerRetryTimer) {
				this.registerRetryTimer = setTimeout(() => {
					this.registerRetryTimer = null;
					this.registerEndpoint();
				}, 250);
			}
			return;
		}

		this.ensureToken();
		const self = this;
		const Endpoint = function () {};
		Endpoint.prototype = {
			supportedMethods: ["POST"],
			supportedDataTypes: ["application/json"],
			init: function (requestData) {
				return self.handleReturn(requestData);
			},
		};
		this.endpointClass = Endpoint;
		Zotero.Server.Endpoints[this.ENDPOINT_PATH] = Endpoint;
		Zotero.debug(`ScanTailor Spectre Loop: registered ${this.ENDPOINT_PATH}`);
	},

	unregisterEndpoint() {
		if (this.registerRetryTimer) {
			clearTimeout(this.registerRetryTimer);
			this.registerRetryTimer = null;
		}
		if (
			this.endpointClass
			&& Zotero.Server?.Endpoints?.[this.ENDPOINT_PATH] === this.endpointClass
		) {
			delete Zotero.Server.Endpoints[this.ENDPOINT_PATH];
		}
		this.endpointClass = null;
		this.token = null;
	},

	patchWindow(win) {
		if (!win || this.windows.has(win)) {
			return;
		}
		const state = {
			installRetryTimer: null,
			menu: null,
			menuitem: null,
			popupHandler: null,
			commandHandler: null,
		};
		this.windows.set(win, state);
		this.installContextMenu(win, state);
	},

	installContextMenu(win, state) {
		const doc = win.document;
		const menu = doc.getElementById("zotero-itemmenu");
		if (!menu) {
			state.installRetryTimer = win.setTimeout(
				() => this.installContextMenu(win, state),
				250
			);
			return;
		}

		doc.getElementById(this.MENU_ID)?.remove();
		const menuitem = doc.createXULElement("menuitem");
		menuitem.id = this.MENU_ID;
		menuitem.setAttribute("label", "Clean scan in ScanTailor Spectre…");
		menuitem.hidden = true;

		const commandHandler = () => {
			this.stageSelectedItem(win).catch((err) => this.reportError(win, err));
		};
		const popupHandler = () => {
			const selected = this.getSelectedItems(win);
			const item = selected.length === 1 ? selected[0] : null;
			menuitem.hidden = !item || !(
				item.isRegularItem?.()
				|| (item.isAttachment?.() && this.isPdfAttachment(item))
			);
		};

		menuitem.addEventListener("command", commandHandler);
		menu.addEventListener("popupshowing", popupHandler);
		menu.appendChild(menuitem);
		state.menu = menu;
		state.menuitem = menuitem;
		state.popupHandler = popupHandler;
		state.commandHandler = commandHandler;
		Zotero.debug("ScanTailor Spectre Loop: installed item context-menu command");
	},

	unpatchWindow(win) {
		const state = this.windows.get(win);
		if (!state) {
			return;
		}
		if (state.installRetryTimer) {
			win.clearTimeout(state.installRetryTimer);
		}
		if (state.menu && state.popupHandler) {
			state.menu.removeEventListener("popupshowing", state.popupHandler);
		}
		if (state.menuitem && state.commandHandler) {
			state.menuitem.removeEventListener("command", state.commandHandler);
		}
		state.menuitem?.remove();
		this.windows.delete(win);
	},

	getSelectedItems(win) {
		try {
			return win.ZoteroPane?.getSelectedItems?.()
				|| win.ZoteroPane_Local?.getSelectedItems?.()
				|| [];
		}
		catch (err) {
			Zotero.debug(`ScanTailor Spectre Loop: could not read selection: ${err}`);
			return [];
		}
	},

	isPdfAttachment(item) {
		return !!(
			item?.isAttachment?.()
			&& (item.isPDFAttachment?.() || item.attachmentContentType === "application/pdf")
		);
	},

	async resolveParentItem(attachment) {
		const parentItemID = attachment?.parentItemID;
		if (!parentItemID) {
			return null;
		}
		let parent = null;
		try {
			parent = Zotero.Items.get(parentItemID);
			if (!parent && Zotero.Items.getAsync) {
				parent = await Zotero.Items.getAsync(parentItemID);
			}
		}
		catch (_) {}
		return parent && !parent.deleted && parent.isRegularItem?.() ? parent : null;
	},

	async readableAttachmentCandidate(attachment) {
		if (!this.isPdfAttachment(attachment) || attachment.deleted) {
			return null;
		}
		let filePath = "";
		try {
			filePath = (await attachment.getFilePathAsync()) || "";
		}
		catch (err) {
			Zotero.debug(`ScanTailor Spectre Loop: could not resolve PDF attachment path: ${err}`);
		}
		if (!this.getReadableLocalFile(filePath)) {
			return null;
		}
		let title = "";
		try {
			title = attachment.getField?.("title") || "";
		}
		catch (_) {}
		return { attachment, path: filePath, label: title || this.basename(filePath) };
	},

	async resolveSelectedRow(win) {
		const selected = this.getSelectedItems(win);
		if (selected.length !== 1) {
			throw new Error("Select exactly one Zotero item or PDF attachment.");
		}
		const selectedItem = selected[0];
		if (this.isPdfAttachment(selectedItem)) {
			const parent = await this.resolveParentItem(selectedItem);
			if (!parent) {
				throw new Error("This PDF has no parent Zotero item. ScanTailor needs a parent item so it knows where to return the cleaned PDF.");
			}
			const candidate = await this.readableAttachmentCandidate(selectedItem);
			if (!candidate) {
				throw new Error("The selected PDF is not available as a readable local file.");
			}
			return { item: parent, candidates: [candidate], selectedIndex: 0 };
		}

		if (!selectedItem?.isRegularItem?.()) {
			throw new Error("Select one regular Zotero item or one of its PDF attachments.");
		}

		const candidates = [];
		for (const attachment of await this.getPdfAttachmentCandidates(selectedItem)) {
			const candidate = await this.readableAttachmentCandidate(attachment);
			if (candidate) {
				candidates.push(candidate);
			}
		}
		if (!candidates.length) {
			throw new Error("This Zotero item has no readable local PDF attachments.");
		}
		return { item: selectedItem, candidates, selectedIndex: 0 };
	},

	async getPdfAttachmentCandidates(item) {
		const candidates = [];
		const seen = new Set();
		const appendIfPDF = (value) => {
			const attachment = typeof value === "number" ? Zotero.Items.get(value) : value;
			if (
				!attachment
				|| attachment.deleted
				|| !this.isPdfAttachment(attachment)
			) {
				return;
			}
			const identity = attachment.id || attachment.key;
			if (!identity || seen.has(identity)) {
				return;
			}
			seen.add(identity);
			candidates.push(attachment);
		};

		try {
			for (const attachment of (await item.getBestAttachments?.()) || []) {
				appendIfPDF(attachment);
			}
		}
		catch (err) {
			Zotero.debug(`ScanTailor Spectre Loop: could not get ranked attachments: ${err}`);
		}

		for (const attachmentID of item.getAttachments?.() || []) {
			appendIfPDF(attachmentID);
		}
		return candidates;
	},

	getReadableLocalFile(path) {
		if (typeof path !== "string" || !path.startsWith("/")) {
			return null;
		}
		try {
			const file = Zotero.File.pathToFile(path);
			return file.exists() && file.isFile() && file.isReadable() ? file : null;
		}
		catch (_) {
			return null;
		}
	},

	slugify(title) {
		const slug = String(title || "")
			.normalize("NFKD")
			.replace(/[\u0300-\u036f]/g, "")
			.toLocaleLowerCase("en-US")
			.replace(/[^\p{L}\p{N}]+/gu, "-")
			.replace(/^-+|-+$/g, "")
			.slice(0, 80)
			.replace(/-+$/g, "");
		return slug || "untitled";
	},

	basename(path) {
		return String(path || "").split(/[\\/]/).pop() || "source.pdf";
	},

	getItemTitle(item) {
		try {
			return item.getField?.("title") || item.getDisplayTitle?.() || "Untitled";
		}
		catch (_) {
			return "Untitled";
		}
	},

	buildSidecar({ item, itemTitle, sourcePdf, token }) {
		return {
			version: 1,
			pluginVersion: addonVersion,
			itemKey: item.key,
			libraryID: item.libraryID,
			itemTitle,
			sourcePdf,
			returnUrl: this.RETURN_URL,
			token,
		};
	},

	getIOUtils() {
		if (typeof IOUtils !== "undefined") {
			return IOUtils;
		}
		return ChromeUtils.importESModule("resource://gre/modules/IOUtils.sys.mjs").IOUtils;
	},

	getPathUtils() {
		if (typeof PathUtils !== "undefined") {
			return PathUtils;
		}
		return ChromeUtils.importESModule("resource://gre/modules/PathUtils.sys.mjs").PathUtils;
	},

	async validateWorkspaceRoot(root) {
		if (typeof root !== "string" || !root.startsWith("/")) {
			throw new Error("Choose an absolute folder for ScanTailor Loop projects.");
		}

		const io = this.getIOUtils();
		const paths = this.getPathUtils();
		if (!(await io.exists(root))) {
			throw new Error(`The ScanTailor Loop folder no longer exists:\n${root}\n\nChoose a different folder.`);
		}

		const probe = paths.join(root, `.st-spectre-write-test-${Zotero.Utilities.randomString(12)}`);
		try {
			await io.writeUTF8(probe, "ScanTailor Spectre write test\n");
			await io.remove(probe);
		}
		catch (_) {
			try {
				if (await io.exists(probe)) {
					await io.remove(probe);
				}
			}
			catch (_) {}
			throw new Error(`ScanTailor Loop cannot write to this folder:\n${root}\n\nChoose a writable folder.`);
		}
		return root;
	},

	async chooseWorkspaceRoot(win, currentRoot = "") {
		const { FilePicker } = ChromeUtils.importESModule(
			"chrome://zotero/content/modules/filePicker.mjs"
		);
		const picker = new FilePicker();
		picker.init(win, "Choose a folder for ScanTailor Loop projects", picker.modeGetFolder);
		if (typeof currentRoot === "string" && currentRoot.startsWith("/")) {
			try {
				picker.displayDirectory = currentRoot;
			}
			catch (_) {}
		}

		const result = await picker.show();
		if (result === picker.returnCancel) {
			return { cancelled: true };
		}
		try {
			const selectedPath = picker.file;
			const root = await this.validateWorkspaceRoot(selectedPath);
			Zotero.Prefs.set(this.PREF_WORKSPACE, root);
			return { path: root };
		}
		catch (err) {
			return { error: err instanceof Error ? err.message : String(err) };
		}
	},

	async getCurrentWorkspaceStatus() {
		const stored = Zotero.Prefs.get(this.PREF_WORKSPACE);
		if (typeof stored === "string" && stored) {
			try {
				return { path: await this.validateWorkspaceRoot(stored), error: "" };
			}
			catch (err) {
				return { path: "", error: err instanceof Error ? err.message : String(err) };
			}
		}
		return { path: "", error: "" };
	},

	async showConfirmationDialog(win, selection) {
		const workspace = await this.getCurrentWorkspaceStatus();
		const model = {
			accepted: false,
			itemTitle: this.getItemTitle(selection.item),
			candidates: selection.candidates.map((candidate) => ({
				label: candidate.label,
				path: candidate.path,
			})),
			selectedIndex: selection.selectedIndex,
			workspaceRoot: workspace.path,
			workspaceError: workspace.error,
			controller: {
				chooseWorkspaceRoot: (dialogWin, currentRoot) => (
					this.chooseWorkspaceRoot(dialogWin, currentRoot)
				),
				validateWorkspaceRoot: (root) => this.validateWorkspaceRoot(root),
			},
		};
		win.openDialog(
			"chrome://st-spectre-loop/content/dialog.xhtml",
			"st-spectre-loop-confirm",
			"chrome,modal,centerscreen,resizable=no",
			model
		);
		if (!model.accepted) {
			return null;
		}
		return {
			workspaceRoot: model.workspaceRoot,
			selectedIndex: model.selectedIndex,
		};
	},

	async createWorkDirectory(itemTitle, root) {
		await this.validateWorkspaceRoot(root);
		const io = this.getIOUtils();
		const paths = this.getPathUtils();
		const slug = this.slugify(itemTitle);
		for (let suffix = 1; suffix < 10000; suffix++) {
			const name = suffix === 1 ? slug : `${slug}-${suffix}`;
			const candidate = paths.join(root, name);
			if (await io.exists(candidate)) {
				continue;
			}
			try {
				await io.makeDirectory(candidate);
				return candidate;
			}
			catch (err) {
				if (!(await io.exists(candidate))) {
					throw err;
				}
			}
		}
		throw new Error("Could not allocate a collision-safe ScanTailor work directory");
	},

	async stageSelectedItem(win) {
		const selection = await this.resolveSelectedRow(win);
		const confirmation = await this.showConfirmationDialog(win, selection);
		if (!confirmation) {
			return null;
		}
		const selected = selection.candidates[confirmation.selectedIndex];
		if (!selected) {
			throw new Error("Choose a source PDF before cleaning the scan.");
		}
		const workspaceRoot = await this.validateWorkspaceRoot(confirmation.workspaceRoot);

		const io = this.getIOUtils();
		const paths = this.getPathUtils();
		const itemTitle = this.getItemTitle(selection.item);
		const workDir = await this.createWorkDirectory(itemTitle, workspaceRoot);
		const sourceBasename = this.basename(selected.path);
		const stagedPdf = paths.join(workDir, sourceBasename);
		await io.copy(selected.path, stagedPdf);
		const sidecar = this.buildSidecar({
			item: selection.item,
			itemTitle,
			sourcePdf: stagedPdf,
			token: this.ensureToken(),
		});
		await io.writeUTF8(
			paths.join(workDir, ".zotero-loop.json"),
			`${JSON.stringify(sidecar, null, "\t")}\n`
		);
		this.rememberJob(selection.item, sourceBasename);
		Zotero.debug(`ScanTailor Spectre Loop: staged ${selection.item.key} at ${workDir}`);
		await this.launchWorkDirectory(workDir);
		return { workDir, sidecar };
	},

	// Several copies of the app can be registered at once (a stale /Applications
	// build, Codex worktree builds); a bare `open -a "ScanTailor Spectre"` may
	// pick any of them, so launch an explicit bundle path when one exists.
	resolveAppPath() {
		const candidates = [
			Zotero.Prefs.get(this.PREF_APP),
			this.DEFAULT_APP_PATH
		].filter(Boolean);
		for (const candidate of candidates) {
			try {
				if (Zotero.File.pathToFile(candidate).exists()) {
					return candidate;
				}
			}
			catch (err) {
				Zotero.debug(`ScanTailor Spectre Loop: app path check failed for ${candidate}: ${err}`);
			}
		}
		return "ScanTailor Spectre";
	},

	async launchWorkDirectory(workDir) {
		try {
			await Zotero.Utilities.Internal.exec(
				"/usr/bin/open",
				["-a", this.resolveAppPath(), workDir]
			);
		}
		catch (err) {
			Zotero.debug(`ScanTailor Spectre Loop: app launch failed: ${err}`);
		}

		try {
			await Zotero.Utilities.Internal.exec("/usr/bin/open", [workDir]);
		}
		catch (err) {
			Zotero.debug(`ScanTailor Spectre Loop: Finder reveal failed: ${err}`);
		}
	},

	resolveStagedParent(itemKey) {
		const job = this.jobs[itemKey];
		if (!job) {
			return null;
		}
		try {
			const item = Zotero.Items.getByLibraryAndKey(job.libraryID, itemKey);
			if (!item || item.deleted || !item.isRegularItem?.()) {
				return null;
			}
			return { item, job };
		}
		catch (err) {
			Zotero.debug(`ScanTailor Spectre Loop: original-item lookup failed: ${err}`);
			return null;
		}
	},

	jsonResponse(status, body) {
		return [status, "application/json", JSON.stringify(body)];
	},

	async handleReturn(requestData) {
		try {
			const headers = requestData?.headers || {};
			const authorization = headers.authorization || headers.Authorization || "";
			if (!this.token || authorization !== `Bearer ${this.token}`) {
				return this.jsonResponse(401, { error: "Unauthorized" });
			}

			const itemKey = String(requestData?.data?.itemKey || "").trim().toUpperCase();
			const filePath = requestData?.data?.filePath;
			if (!/^[A-Z0-9]{8}$/.test(itemKey) || typeof filePath !== "string") {
				return this.jsonResponse(400, { error: "Missing itemKey or filePath" });
			}

			const localFile = this.getReadableLocalFile(filePath);
			if (!localFile) {
				return this.jsonResponse(400, { error: "filePath is not an absolute, readable file" });
			}

			const original = this.resolveStagedParent(itemKey);
			if (!original) {
				return this.jsonResponse(404, { error: "Unknown itemKey" });
			}

			const attachment = await Zotero.Attachments.importFromFile({
				file: localFile,
				parentItemID: original.item.id,
				title: `${original.job.sourceBasename} (cleaned)`,
			});
			try {
				await Zotero.Fulltext.indexItems([attachment.id], { ignoreErrors: true });
			}
			catch (err) {
				Zotero.debug(`ScanTailor Spectre Loop: full-text indexing trigger failed: ${err}`);
			}
			Zotero.debug(`ScanTailor Spectre Loop: attached cleaned PDF ${attachment.key} to ${itemKey}`);
			return this.jsonResponse(200, { attachmentKey: attachment.key });
		}
		catch (err) {
			Zotero.debug(`ScanTailor Spectre Loop: return request failed: ${err instanceof Error ? err.message : String(err)}`);
			return this.jsonResponse(500, { error: "Internal error" });
		}
	},

	reportError(win, err) {
		const message = err instanceof Error ? err.message : String(err);
		Zotero.debug(`ScanTailor Spectre Loop: ${message}`);
		try {
			Services.prompt.alert(win, "ScanTailor Spectre Loop", message);
		}
		catch (_) {}
	},
};
