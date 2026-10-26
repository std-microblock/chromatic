#include "pch.h"
#include "PluginManager.h"

#include <utils/utils.h>

#include "resource.h"


extern const std::string version;
std::map<std::string, std::shared_ptr<PluginNativeAPI>> plugin_native_apis;

std::vector<std::shared_ptr<Plugin>> PluginManager::packedPlugins;

auto ncmVer = util::getNCMExecutableVersion();
const unsigned short ncmVersion[3] = { ncmVer.major, ncmVer.minor, ncmVer.patch };
namespace fs = std::filesystem;

int addNativeAPI(NativeAPIType args[], int argsNum, const char* identifier, char* function(void**)) {
	plugin_native_apis[std::string(identifier)] =
		std::make_shared<PluginNativeAPI>(PluginNativeAPI{ args, argsNum, std::string(identifier), function });
	return true;
}

int addNativeAPIEmpty(NativeAPIType args[], int argsNum, const char* identifier, char* function(void**)) {
	return false;
}

extern BNString datapath;

void from_json(const nlohmann::json& j, RemotePlugin& plugin) {
	plugin.name = j.value("name", "unknown");
	plugin.author = j.value("author", "unknown");
	plugin.version = j.value("version", "unknown");
	plugin.description = j.value("description", "unknown");
	plugin.betterncm_version = j.value("betterncm_version", "unknown");
	plugin.preview = j.value("preview", "unknown");
	plugin.slug = j.value("slug", "unknown");
	plugin.update_time = j.value("update_time", 0);
	plugin.publish_time = j.value("publish_time", 0);
	plugin.repo = j.value("repo", "unknown");
	plugin.file = j.value("file", "unknown");
	plugin.file_url = j.value("file-url", "unknown");
	plugin.force_install = j.value("force-install", false);
	plugin.force_uninstall = j.value("force-uninstall", false);
	plugin.force_update = j.value("force-update", "< 0.0.0");
}


void from_json(const nlohmann::json& j, PluginManifest& p) {
	p.manifest_version = j.value("manifest_version", 0);
	p.name = j.value("name", "unknown");

	auto getSlugName = [](std::string name) {
		if (name.empty()) return name;
		std::replace(name.begin(), name.end(), ' ', '-');
		try {
			std::erase_if(name, [](char c) { return c > 0 && c < 255 && !isalnum(c) && c != '-'; });
		}
		catch (std::exception& e) {
		}
		return name;
	};

	p.slug = j.value("slug", getSlugName(p.name));
	p.version = j.value("version", "unknown");
	p.author = j.value("author", "unknown");
	p.description = j.value("description", "unknown");
	p.betterncm_version = j.value("betterncm_version", ">=1.0.0");
	p.preview = j.value("preview", "unknown");
	p.injects = j.value("injects", std::map<std::string, std::vector<std::map<std::string, std::string>>>());
	p.startup_script = j.value("startup_script", "startup_script.js");
	p.ncm3Compatible = j.value("ncm3-compatible", false);
	p.ncm_version_req = j.value("ncm-version-req", "> 2.10.2");

	p.hijacks.clear();
	if (j.count("hijacks")) {
		const auto& hijack_version_map = j.at("hijacks");
		for (auto version_it = hijack_version_map.begin(); version_it != hijack_version_map.end(); ++version_it) {
			const auto& hijack_version = version_it.key();
			HijackURLMap hijack_url_map;
			const auto& hijack_url_map_json = version_it.value();
			for (auto url_it = hijack_url_map_json.begin(); url_it != hijack_url_map_json.end(); ++url_it) {
				const auto& hijack_url = url_it.key();
				std::vector<HijackAction> hijack_actions;
				auto& hijack_actions_json = url_it.value();

				auto process_hijack_entry = [](nlohmann::json hijack_action_json) -> HijackAction {
					const std::string type = hijack_action_json.at("type").get<std::string>();
					const std::string id = hijack_action_json.value("id", "");

					if (type == "regex") {
						return HijackActionRegex{
							id ,
							hijack_action_json.at("from").get<std::string>(),
							hijack_action_json.at("to").get<std::string>()
							};
					}

					if (type == "replace") {
						return HijackActionReplace{
							id ,
							hijack_action_json.at("from").get<std::string>(),
							hijack_action_json.at("to").get<std::string>()
							};
					}

					if (type == "append") {
						return HijackActionAppend{
							id ,
							hijack_action_json.at("code").get<std::string>()
							};
					}

					if (type == "prepend") {
						return HijackActionPrepend{
							 id ,
							hijack_action_json.at("code").get<std::string>()
							};
					}
				};

				if (url_it->is_object()) {
					hijack_actions.push_back(process_hijack_entry(url_it.value()));
				}
				else {
					for (auto action_it = hijack_actions_json.begin(); action_it != hijack_actions_json.end(); ++action_it) 
						hijack_actions.push_back(process_hijack_entry(*action_it));
				}
				
				hijack_url_map[hijack_url] = hijack_actions;
			}
			p.hijacks[hijack_version] = hijack_url_map;
		}
	}

	p.native_plugin = j.value("native_plugin", "\0");
}

Plugin::Plugin(PluginManifest manifest,
	std::filesystem::path runtime_path,
	std::optional<std::filesystem::path> packed_file_path)
		: manifest(std::move(manifest)), runtime_path(std::move(runtime_path))
			, packed_file_path(std::move(packed_file_path)) {
}

Plugin::~Plugin() {
	/*if (this->hNativeDll) {
		this->hNativeDll = nullptr;
		FreeLibrary(this->hNativeDll);
	}*/
}

void Plugin::loadNativePluginDll(NCMProcessType processType) {

	if (manifest.native_plugin[0] != '\0') {
		try {
			HMODULE hDll = LoadLibrary((runtime_path / manifest.native_plugin).wstring().c_str());
			if (!hDll) {
				const fs::path x64path = runtime_path / fs::path(manifest.native_plugin).parent_path() / (fs::path(manifest.native_plugin).filename().string() + ".x64.dll");
				std::cout << "NativePlugin x64path: " << (x64path.string());
				hDll = LoadLibrary(x64path.wstring().c_str());
			}

			if (!hDll) {
				throw std::exception("dll doesn't exists or is not adapted to this arch.");
			}

			auto BetterNCMPluginMain = (BetterNCMPluginMainFunc)GetProcAddress(hDll, "BetterNCMPluginMain");
			if (!BetterNCMPluginMain) {
				throw std::exception("dll is not a betterncm plugin dll");
			}


			auto pluginAPI = new BetterNCMNativePlugin::PluginAPI{
					processType & Renderer ? addNativeAPI : addNativeAPIEmpty,
					version.c_str(),
					processType,
					&ncmVersion
			}; // leaked but not a big problem

			BetterNCMPluginMain(pluginAPI);
			this->hNativeDll = hDll;
		}
		catch (std::exception& e) {
			util::write_file_text(datapath.utf8() + "/log.log",
				std::string("\n[" + manifest.slug + "] Plugin Native Plugin load Error: ") + (e.
					what()), true);
		}
	}
}

std::optional<std::string> Plugin::getStartupScript()
{
	if(fs::exists(runtime_path / manifest.startup_script))
		return util::read_to_string_utf8(runtime_path / manifest.startup_script).utf8();
	return std::nullopt;
}

void PluginManager::performForceInstallAndUpdateAsync(const std::string& source)
{
	std::thread([source]() {
		try {
			performForceInstallAndUpdateSync(source);
		}
		catch (const std::exception& e) {
			std::cerr << "[ BetterNCM ] [Plugin Remote Tasks] Update failed on "
				<< source << ": " << e.what() << std::endl;
		}
		catch (...) {
			std::cerr << "[ BetterNCM ] [Plugin Remote Tasks] Unknown update error on "
				<< source << std::endl;
		}
	}).detach();
}

void PluginManager::loadAll() {
	unloadAll();
	loadRuntime();
}

void PluginManager::unloadAll() {
	plugin_native_apis.clear();
	packedPlugins.clear();
}

void PluginManager::loadRuntime() {
	packedPlugins = loadInPath(datapath + L"/plugins_runtime");
}

void PluginManager::extractPackedPlugins() {
	util::write_file_text(datapath + L"/PLUGIN_EXTRACTING_LOCK.lock", "");

	const auto disable_list = PluginManager::getDisableList();

	if (fs::exists(datapath + L"/plugins_runtime")) {
		for (auto file : fs::directory_iterator(datapath + L"/plugins_runtime")) {
			try {
				PluginManifest manifest;
				auto modManifest = nlohmann::json::parse(util::read_to_string(file.path() / "manifest.json"));
				modManifest.get_to(manifest);

				if (manifest.native_plugin[0] == '\0')
					remove_all(file.path());
				else {
					std::error_code ec;
					fs::remove(file.path() / manifest.native_plugin, ec);
					if (ec.value() == 0)remove_all(file.path());
				}
			}
			catch (std::exception& e) {
				remove_all(file.path());
			}
		}
	}

	fs::create_directories(datapath + L"/plugins_runtime");
	static const bool isNCM3 = util::getNCMExecutableVersion().major == 3;

	for (auto file : fs::directory_iterator(datapath + L"/plugins")) {
		BNString path = file.path().wstring();
		if (path.endsWith(L".plugin")) {
			try {

				PluginManifest manifest;

				const auto extractPlugin = [&]() {
					if(fs::exists(datapath.utf8() + "/plugins_runtime/tmp")) 
						fs::remove_all(datapath.utf8() + "/plugins_runtime/tmp");
					
					const auto zip = zip_open(path.utf8().c_str(), 0, 'r');
					
					auto code = zip_entry_open(zip, "manifest.json");
					if (code < 0) throw std::exception("manifest.json not found in plugin");

					char* buf = nullptr;
					size_t size = 0;
					code = zip_entry_read(zip, (void**) & buf, &size);
					if (code < 0) throw std::exception("manifest.json read error");
					zip_entry_close(zip);
					zip_close(zip);

					const auto modManifest = nlohmann::json::parse(std::string(buf, size));
					modManifest.get_to(manifest);
					};

				extractPlugin();
				if (manifest.name == "PluginMarket") {
					if (semver::version(manifest.version) < semver::version("0.7.2")) {
						util::extractPluginMarket();
						extractPlugin();
					}
				}

				if (std::ranges::find(disable_list, manifest.slug) != disable_list.end() ||
					(
					isNCM3 &&
					!manifest.ncm3Compatible // duplicated / ncm3 but not ncm3-compatible / do not meet version req
					) ||
					(
						!semver::range::satisfies(
							util::getNCMExecutableVersion(), manifest.ncm_version_req
						)
					)
					) {
					continue;
				}

				if (manifest.manifest_version == 1) {
					BNString realPath = datapath + L"/plugins_runtime/" + BNString(manifest.slug);

					std::error_code ec;
					if (fs::exists(realPath.utf8()) && manifest.native_plugin[0] == '\0')
						fs::remove_all(realPath.utf8(), ec);

					const auto code = zip_extract(path.utf8().c_str(), realPath.utf8().c_str(), nullptr, nullptr);
					if (code != 0) throw std::exception(("unzip err code:" + std::to_string(code)).c_str());

					util::write_file_text(realPath + L"/.plugin.path.meta",
						pystring::slice(path, datapath.length()));
				}
				else {
					throw std::exception("Unsupported manifest version.");
				}
			}
			catch (std::exception& e) {
				std::cout << BNString::fromGBK(std::string("\n[BetterNCM] Plugin Loading Error: ") + (e.what())).utf8()
					+ "\n";
				fs::remove_all(datapath.utf8() + "/plugins_runtime/tmp");
			}
		}
	}

	fs::remove(datapath + L"/PLUGIN_EXTRACTING_LOCK.lock");
}

std::vector<std::shared_ptr<Plugin>> PluginManager::getDevPlugins()
{
	return loadInPath(datapath + L"/plugins_dev");
}


std::vector<std::shared_ptr<Plugin>> PluginManager::getAllPlugins() 
{
	std::vector<std::shared_ptr<Plugin>> tmp = getPackedPlugins();
	auto devPlugins = getDevPlugins();
	tmp.insert(tmp.end(), devPlugins.begin(), devPlugins.end());

	std::sort(tmp.begin(), tmp.end(), [](const auto& a, const auto& b) {
		return a->manifest.slug < b->manifest.slug;
		});

	auto last = std::unique(tmp.begin(), tmp.end(), [](const auto& a, const auto& b) {
		return a->manifest.slug == b->manifest.slug;
		});

	tmp.erase(last, tmp.end());

	return tmp;
}

std::vector<std::shared_ptr<Plugin>> PluginManager::getPackedPlugins()
{
	return PluginManager::packedPlugins;
}

std::vector<std::string> PluginManager::getDisableList()
{
	std::vector<std::string> disable_list;
	std::ifstream file(datapath + L"/disable_list.txt");
	if (!file.is_open()) {
		return disable_list;
	}

	std::string line;
	while (std::getline(file, line)) {
		// Trim leading and trailing white space characters
		auto isspace = [](char c) { return std::isspace(static_cast<unsigned char>(c)); };
		line.erase(line.begin(), std::find_if_not(line.begin(), line.end(), isspace));
		line.erase(std::find_if_not(line.rbegin(), line.rend(), isspace).base(), line.end());

		disable_list.push_back(line);
	}
	file.close();
	return disable_list;
}

std::vector<std::shared_ptr<Plugin>> PluginManager::loadInPath(const std::wstring& path) {
	std::vector<std::shared_ptr<Plugin>> plugins;
	if (fs::exists(path))
		for (const auto& file : fs::directory_iterator(path)) {
			try {
				if (fs::exists(file.path() / "manifest.json")) {
					auto json = nlohmann::json::parse(util::read_to_string(file.path() / "manifest.json"));
					PluginManifest manifest;
					json.get_to(manifest);

					std::optional<std::filesystem::path> packed_file_path = std::nullopt;
					auto plugin_meta_path = file.path() / ".plugin.path.meta";
					if (fs::exists(plugin_meta_path)) packed_file_path = util::read_to_string(plugin_meta_path);
					plugins.push_back(std::make_shared<Plugin>(manifest, file.path(), packed_file_path));
				}
			}
			catch (std::exception& e) {
				util::write_file_text(datapath.utf8() + "log.log",
					std::string("\n[" + file.path().string() + "] Plugin Native load Error: ") + (e.
						what()), true);
			}
		}

	return plugins;
}

void PluginManager::performForceInstallAndUpdateSync(const std::string& source, bool isRetried)
{
	try {
		const auto body = util::FetchWebContent(source + "plugins.json");
		nlohmann::json plugins_json = nlohmann::json::parse(body);
		std::vector<RemotePlugin> remote_plugins;
		plugins_json.get_to(remote_plugins);
		const auto local_plugins = getPackedPlugins();

		for (const auto& remote_plugin : remote_plugins) {
			const auto local = std::find_if(local_plugins.begin(), local_plugins.end(), [&remote_plugin](const auto& local) {
				return remote_plugin.slug == local->manifest.slug;
				});

			// output log
			std::cout << "\n[ BetterNCM ] [Plugin Remote Tasks] Plugin " << remote_plugin.slug
			    << " FUni: " << (remote_plugin.force_uninstall ? "true" : "false")
				<< " FUpd: " << remote_plugin.force_update << "\n";

			if (local != local_plugins.end()) {
				auto localVer = (*local)->manifest.version;
				std::cout << "\t\tlocal: " << localVer << "\n\t\t - at " << (*local)->runtime_path << std::endl;

				auto packed_file_path_relative = (*local)->packed_file_path;
				if (packed_file_path_relative.has_value()) {
					std::cout << "\t\t - at " << packed_file_path_relative.value() << std::endl;
					auto origin_packed_plugin_path = datapath.utf8() / packed_file_path_relative.value();
					if (remote_plugin.force_uninstall) {
						fs::remove(origin_packed_plugin_path);
						std::cout << "\t\t - Force uninstall performed.\n";
						std::cout << std::endl;
					}

					try {
						if ((remote_plugin.force_update == "*" || semver::range::satisfies(semver::from_string(localVer), remote_plugin.force_update)) &&
							localVer != remote_plugin.version) {
							std::cout << "\t\t - Force update: Downloading...\n";

							const auto dest = datapath + L"/plugins/" + BNString(remote_plugin.file);
							if (fs::exists(dest)) fs::remove(dest);
							if (fs::exists(origin_packed_plugin_path)) fs::remove(origin_packed_plugin_path);
							util::DownloadFile(source + remote_plugin.file_url, dest);
							std::cout << "\t\t - Force update performed.\n";
							std::cout << std::endl;
						}
					}
					catch (std::exception& e) {
						std::cout << "[ BetterNCM ] [Plugin Remote Tasks] Failed to check update for remote plugin " << remote_plugin.slug << ": " << e.what() << std::endl;
					}
				}
			}
		}
	}
	catch (std::exception& e) {
		if(isRetried) {
			std::cout << "[ BetterNCM ] [Plugin Remote Tasks] Failed to check update on " << source << ": " << e.what() << "." << std::endl;
		}else {
			const auto onlineConfig = util::FetchWebContent("https://microblock.cc/bncm-config.txt");
			const auto marketConf = onlineConfig.split(L"\n")[0];
			std::cout << "[ BetterNCM ] [Plugin Remote Tasks] Failed to check update on " << source << ": " << e.what() << " , fallbacking to default..." << std::endl;
			performForceInstallAndUpdateSync(BNString(marketConf).utf8(), true);
		}
		
	}
	
}
