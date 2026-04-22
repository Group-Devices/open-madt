#ifndef UTILS_JSON_FILE_HPP
#define UTILS_JSON_FILE_HPP

#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include <loghelper/log.h>

using json = nlohmann::json;

namespace Secretary {
	namespace utils {

		enum class JsonFileLoadResult
		{
			Loaded,
			Missing,
			Error
		};

		inline JsonFileLoadResult
		loadJsonFile(const std::filesystem::path& path, json& document, const char* description)
		{
			std::ifstream ifs(path);
			if (!ifs.is_open()) {
				return JsonFileLoadResult::Missing;
			}

			try {
				document = json::parse(ifs);
				return JsonFileLoadResult::Loaded;
			} catch (const json::parse_error& e) {
				ELOG("Can't parse %s %s: %s", description, path.c_str(), e.what());
			} catch (const std::exception& e) {
				ELOG("Exception loading %s %s: %s", description, path.c_str(), e.what());
			}
			return JsonFileLoadResult::Error;
		}

		inline bool loadRequiredJsonFile(const std::filesystem::path& path,
		                                 json&                        document,
		                                 const char*                  description)
		{
			const auto result = loadJsonFile(path, document, description);
			if (result == JsonFileLoadResult::Missing) {
				ELOG("Failed to open %s %s", description, path.c_str());
				return false;
			}
			return result == JsonFileLoadResult::Loaded;
		}

		inline JsonFileLoadResult loadOptionalJsonFile(const std::filesystem::path& path,
		                                               json&                        document,
		                                               const char*                  description)
		{
			return loadJsonFile(path, document, description);
		}

	} // namespace utils
} // namespace Secretary

#endif // UTILS_JSON_FILE_HPP
