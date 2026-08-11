let chromeHandle;

function install(data, reason) {}

async function startup({ id, version, resourceURI, rootURI }, reason) {
	const aomStartup = Components.classes[
		"@mozilla.org/addons/addon-manager-startup;1"
	].getService(Components.interfaces.amIAddonManagerStartup);

	const manifestURI = Services.io.newURI(rootURI + "manifest.json");
	chromeHandle = aomStartup.registerChrome(manifestURI, [
		["content", "st-spectre-loop", rootURI + "content/"],
	]);

	const ctx = {
		addonID: id,
		addonVersion: version,
		rootURI,
	};
	ctx._globalThis = ctx;

	Services.scriptloader.loadSubScript(
		rootURI + "content/scripts/st-spectre-loop.js",
		ctx
	);

	await ctx.STSpectreLoop.hooks.onStartup();
}

async function onMainWindowLoad({ window }, reason) {
	await Zotero.STSpectreLoop?.hooks.onMainWindowLoad(window);
}

async function onMainWindowUnload({ window }, reason) {
	await Zotero.STSpectreLoop?.hooks.onMainWindowUnload(window);
}

async function shutdown({ id, version, resourceURI, rootURI }, reason) {
	if (reason === APP_SHUTDOWN) {
		return;
	}

	await Zotero.STSpectreLoop?.hooks.onShutdown();

	if (chromeHandle) {
		chromeHandle.destruct();
		chromeHandle = null;
	}
}

async function uninstall(data, reason) {}

