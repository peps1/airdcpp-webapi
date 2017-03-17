/*
* Copyright (C) 2011-2017 AirDC++ Project
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#include <web-server/stdinc.h>
#include <api/ApiSettingItem.h>
#include <web-server/JsonUtil.h>

#include <airdcpp/AirUtil.h>
#include <airdcpp/ConnectionManager.h>
#include <airdcpp/ConnectivityManager.h>
#include <airdcpp/ResourceManager.h>
#include <airdcpp/SearchManager.h>
#include <airdcpp/SettingHolder.h>
#include <airdcpp/SettingItem.h>
#include <airdcpp/SettingsManager.h>
#include <airdcpp/StringTokenizer.h>

namespace webserver {
	const ApiSettingItem::MinMax ApiSettingItem::defaultMinMax = { 0, MAX_INT_VALUE };

	ApiSettingItem::ApiSettingItem(const string& aName, Type aType) :
		name(aName), type(aType) {

	}

	string ApiSettingItem::typeToStr(Type aType) noexcept {
		switch (aType) {
			case TYPE_BOOLEAN: return "boolean";
			case TYPE_NUMBER: return "number";
			case TYPE_STRING: return "string";
			case TYPE_FILE_PATH: return "file_path";
			case TYPE_DIRECTORY_PATH: return "directory_path";
			case TYPE_TEXT: return "text";
		}

		dcassert(0);
		return Util::emptyString;
	}

	json ApiSettingItem::infoToJson(bool aForceAutoValues) const noexcept {
		auto value = valueToJson(aForceAutoValues);

		// Serialize the setting
		json ret;
		ret["title"] = getTitle();
		if (value.second) {
			ret["auto"] = true;
		}

		ret["type"] = typeToStr(type);

		if (type == TYPE_NUMBER) {
			const auto& minMax = getMinMax();
			
			if (minMax.min != 0) {
				ret["min"] = minMax.min;
			}

			if (minMax.max != MAX_INT_VALUE) {
				ret["max"] = minMax.max;
			}
		}

		if (isOptional()) {
			ret["optional"] = true;
		}

		return ret;
	}

	ServerSettingItem::ServerSettingItem(const string& aKey, const string& aTitle, const json& aDefaultValue, Type aType, bool aOptional, const MinMax& aMinMax) :
		ApiSettingItem(aKey, aType), desc(aTitle), defaultValue(aDefaultValue), value(aDefaultValue), optional(aOptional), minMax(aMinMax) {

	}

	ApiSettingItem::Type ServerSettingItem::deserializeType(const string& aTypeStr) noexcept {
		if (aTypeStr == "string") {
			return TYPE_STRING;
		} else if (aTypeStr == "boolean") {
			return TYPE_BOOLEAN;
		} else if (aTypeStr == "number") {
			return TYPE_NUMBER;
		} else if (aTypeStr == "text") {
			return TYPE_TEXT;
		} else if (aTypeStr == "file_path") {
			return TYPE_FILE_PATH;
		} else if (aTypeStr == "directory_path") {
			return TYPE_DIRECTORY_PATH;
		}

		dcassert(0);
		return TYPE_LAST;
	}

	ServerSettingItem ServerSettingItem::fromJson(const json& aJson) {
		auto key = JsonUtil::getField<string>("key", aJson, false);
		auto title = JsonUtil::getField<string>("title", aJson, false);

		auto typeStr = JsonUtil::getField<string>("type", aJson, false);
		auto type = deserializeType(typeStr);
		if (type == TYPE_LAST) {
			JsonUtil::throwError("type", JsonUtil::ERROR_INVALID, "Invalid type " + typeStr);
		}

		auto isOptional = JsonUtil::getOptionalFieldDefault<bool>("optional", aJson, false);
		if (isOptional && (type == TYPE_BOOLEAN || type == TYPE_NUMBER)) {
			JsonUtil::throwError("optional", JsonUtil::ERROR_INVALID, "Field of type " + typeStr + " can't be optional");
		}

		auto defaultValue = JsonUtil::getOptionalRawField("defaultValue", aJson, !isOptional);

		auto minValue = JsonUtil::getOptionalFieldDefault<int>("min", aJson, 0);
		auto maxValue = JsonUtil::getOptionalFieldDefault<int>("max", aJson, MAX_INT_VALUE);

		return ServerSettingItem(key, title, defaultValue, type, isOptional, { minValue, maxValue });
	}

	json ServerSettingItem::infoToJson(bool aForceAutoValues) const noexcept {
		return ApiSettingItem::infoToJson(aForceAutoValues);
	}

	// Returns the value and bool indicating whether it's an auto detected value
	pair<json, bool> ServerSettingItem::valueToJson(bool /*aForceAutoValues*/) const noexcept {
		return { value, false };
	}

	void ServerSettingItem::unset() noexcept {
		value = defaultValue;
	}

	bool ServerSettingItem::setCurValue(const json& aJson) {
		if (aJson.is_null()) {
			unset();
		} else {
			JsonUtil::ensureType(name, aJson, defaultValue);

			if (aJson.is_number()) {
				const int num = aJson;
				JsonUtil::validateRange(name, num, getMinMax().min, getMinMax().max);
			}

			value = aJson;
		}

		return true;
	}

	int ServerSettingItem::num() {
		return value.get<int>();
	}

	uint64_t ServerSettingItem::uint64() {
		return value.get<uint64_t>();
	}

	string ServerSettingItem::str() {
		if (value.is_number()) {
			return Util::toString(num());
		}

		return value.get<string>();
	}

	bool ServerSettingItem::boolean() {
		return value.get<bool>();
	}

	bool ServerSettingItem::isDefault() const noexcept {
		return value == defaultValue;
	}


	const ApiSettingItem::MinMax& ServerSettingItem::getMinMax() const noexcept {
		return minMax;
	}

	map<int, CoreSettingItem::Group> groupMappings = {
		{ SettingsManager::TCP_PORT, CoreSettingItem::GROUP_CONN_GEN },
		{ SettingsManager::UDP_PORT, CoreSettingItem::GROUP_CONN_GEN },
		{ SettingsManager::TLS_PORT, CoreSettingItem::GROUP_CONN_GEN },
		{ SettingsManager::MAPPER, CoreSettingItem::GROUP_CONN_GEN },

		{ SettingsManager::BIND_ADDRESS, CoreSettingItem::GROUP_CONN_V4 },
		{ SettingsManager::INCOMING_CONNECTIONS, CoreSettingItem::GROUP_CONN_V4 },
		{ SettingsManager::EXTERNAL_IP, CoreSettingItem::GROUP_CONN_V4 },
		{ SettingsManager::IP_UPDATE, CoreSettingItem::GROUP_CONN_V4 },
		{ SettingsManager::NO_IP_OVERRIDE, CoreSettingItem::GROUP_CONN_V4 },

		{ SettingsManager::BIND_ADDRESS6, CoreSettingItem::GROUP_CONN_V6 },
		{ SettingsManager::INCOMING_CONNECTIONS6, CoreSettingItem::GROUP_CONN_V6 },
		{ SettingsManager::EXTERNAL_IP6, CoreSettingItem::GROUP_CONN_V6 },
		{ SettingsManager::IP_UPDATE6, CoreSettingItem::GROUP_CONN_V6 },
		{ SettingsManager::NO_IP_OVERRIDE6, CoreSettingItem::GROUP_CONN_V6 },

		{ SettingsManager::DOWNLOAD_SLOTS, CoreSettingItem::GROUP_LIMITS_DL },
		{ SettingsManager::MAX_DOWNLOAD_SPEED, CoreSettingItem::GROUP_LIMITS_DL },

		{ SettingsManager::MIN_UPLOAD_SPEED, CoreSettingItem::GROUP_LIMITS_UL },
		{ SettingsManager::AUTO_SLOTS, CoreSettingItem::GROUP_LIMITS_UL },
		{ SettingsManager::SLOTS, CoreSettingItem::GROUP_LIMITS_UL },

		{ SettingsManager::MAX_MCN_DOWNLOADS, CoreSettingItem::GROUP_LIMITS_MCN },
		{ SettingsManager::MAX_MCN_UPLOADS, CoreSettingItem::GROUP_LIMITS_MCN },
	};

	map<int, CoreSettingItem::MinMax > minMaxMappings = {
		{ SettingsManager::TCP_PORT, { 1, 65535 } },
		{ SettingsManager::UDP_PORT, { 1, 65535 } },
		{ SettingsManager::TLS_PORT, { 1, 65535 } },

		{ SettingsManager::MAX_HASHING_THREADS, { 1, 100 } },
		{ SettingsManager::HASHERS_PER_VOLUME, { 1, 100 } },

		// No validation for other enums at the moment but negative value would cause issues otherwise...
		{ SettingsManager::INCOMING_CONNECTIONS, { SettingsManager::INCOMING_DISABLED, SettingsManager::INCOMING_LAST } },
		{ SettingsManager::INCOMING_CONNECTIONS6, { SettingsManager::INCOMING_DISABLED, SettingsManager::INCOMING_LAST } },
	};

	set<int> optionalSettingKeys = {
		SettingsManager::DESCRIPTION,
		SettingsManager::EMAIL,

		SettingsManager::EXTERNAL_IP,
		SettingsManager::EXTERNAL_IP6,

		SettingsManager::DEFAULT_AWAY_MESSAGE,
		SettingsManager::SKIPLIST_DOWNLOAD,
		SettingsManager::SKIPLIST_SHARE,
		SettingsManager::FREE_SLOTS_EXTENSIONS,
	};

	CoreSettingItem::CoreSettingItem(const string& aName, int aKey, ResourceManager::Strings aDesc, Type aType, ResourceManager::Strings aUnit) :
		ApiSettingItem(aName, parseAutoType(aType, aKey)), SettingItem({ aKey, aDesc }), unit(aUnit) {

	}

	ApiSettingItem::Type CoreSettingItem::parseAutoType(Type aType, int aKey) noexcept {
		if (aKey >= SettingsManager::STR_FIRST && aKey < SettingsManager::STR_LAST) {
			if (aType == TYPE_LAST) return TYPE_STRING;
			dcassert(isString(aType));
		} else if (aKey >= SettingsManager::INT_FIRST && aKey < SettingsManager::INT_LAST) {
			if (aType == TYPE_LAST) return TYPE_NUMBER;
			dcassert(aType == TYPE_NUMBER);
		} else if (aKey >= SettingsManager::BOOL_FIRST && aKey < SettingsManager::BOOL_LAST) {
			if (aType == TYPE_LAST) return TYPE_BOOLEAN;
			dcassert(aType == TYPE_BOOLEAN);
		} else {
			dcassert(0);
		}

		return aType;
	}


	#define USE_AUTO(aType, aSetting) ((groupMappings.find(SettingsManager::aSetting) != groupMappings.end() && groupMappings.at(SettingsManager::aSetting) == aType) && (aForceAutoValues || SETTING(aSetting)))
	json CoreSettingItem::autoValueToJson(bool aForceAutoValues) const noexcept {
		json v;
		if (USE_AUTO(GROUP_CONN_V4, AUTO_DETECT_CONNECTION) || USE_AUTO(GROUP_CONN_V6, AUTO_DETECT_CONNECTION6) ||
			(type == GROUP_CONN_GEN && (SETTING(AUTO_DETECT_CONNECTION) || SETTING(AUTO_DETECT_CONNECTION6)))) {

			if (key == SettingsManager::TCP_PORT) {
				v = ConnectionManager::getInstance()->getPort();
			} else if (key == SettingsManager::UDP_PORT) {
				v = SearchManager::getInstance()->getPort();
			} else if (key == SettingsManager::TLS_PORT) {
				v = ConnectionManager::getInstance()->getSecurePort();
			} else {
				if (isString(type)) {
					v = ConnectivityManager::getInstance()->get(static_cast<SettingsManager::StrSetting>(key));
				} else if (type == TYPE_NUMBER) {
					v = ConnectivityManager::getInstance()->get(static_cast<SettingsManager::IntSetting>(key));
				} else if (type == TYPE_BOOLEAN) {
					v = ConnectivityManager::getInstance()->get(static_cast<SettingsManager::BoolSetting>(key));
				} else {
					dcassert(0);
				}
			}
		} else if (USE_AUTO(GROUP_LIMITS_DL, DL_AUTODETECT)) {
			if (key == SettingsManager::DOWNLOAD_SLOTS) {
				v = AirUtil::getSlots(true);
			} else if (key == SettingsManager::MAX_DOWNLOAD_SPEED) {
				v = AirUtil::getSpeedLimit(true);
			}
		} else if (USE_AUTO(GROUP_LIMITS_UL, UL_AUTODETECT)) {
			if (key == SettingsManager::SLOTS) {
				v = AirUtil::getSlots(false);
			} else if (key == SettingsManager::MIN_UPLOAD_SPEED) {
				v = AirUtil::getSpeedLimit(false);
			} else if (key == SettingsManager::AUTO_SLOTS) {
				v = AirUtil::getMaxAutoOpened();
			}
		} else if (USE_AUTO(GROUP_LIMITS_MCN, MCN_AUTODETECT)) {
			v = AirUtil::getSlotsPerUser(key == SettingsManager::MAX_MCN_DOWNLOADS);
		}

		return v;
	}

	const ApiSettingItem::MinMax& CoreSettingItem::getMinMax() const noexcept {
		auto i = minMaxMappings.find(key);
		return i != minMaxMappings.end() ? i->second : defaultMinMax;
	}

	bool CoreSettingItem::isOptional() const noexcept {
		return optionalSettingKeys.find(key) != optionalSettingKeys.end();
	}

	pair<json, bool> CoreSettingItem::valueToJson(bool aForceAutoValues) const noexcept {
		auto v = autoValueToJson(aForceAutoValues);
		if (!v.is_null()) {
			return { v, true };
		}

		if (isString(type)) {
			v = SettingsManager::getInstance()->get(static_cast<SettingsManager::StrSetting>(key), true);
		} else if (type == TYPE_NUMBER) {
			v = SettingsManager::getInstance()->get(static_cast<SettingsManager::IntSetting>(key), true);
		} else if (type == TYPE_BOOLEAN) {
			v = SettingsManager::getInstance()->get(static_cast<SettingsManager::BoolSetting>(key), true);
		} else {
			dcassert(0);
		}

		return { v, false };
	}

	json CoreSettingItem::infoToJson(bool aForceAutoValues) const noexcept {
		// Get the current value
		auto value = valueToJson(aForceAutoValues);

		// Serialize the setting
		json ret = ApiSettingItem::infoToJson(aForceAutoValues);

		// Unit
		if (unit != ResourceManager::LAST) {
			ret["unit"] = ResourceManager::getInstance()->getString(unit);
		}

		// Serialize possible enum values
		auto enumStrings = SettingsManager::getEnumStrings(key, false);
		if (!enumStrings.empty()) {
			for (const auto& i : enumStrings) {
				ret["values"].push_back({
					{ "id", i.first },
					{ "name", ResourceManager::getInstance()->getString(i.second) },
				});
			}
		} else if (key == SettingsManager::BIND_ADDRESS || key == SettingsManager::BIND_ADDRESS6) {
			auto bindAddresses = AirUtil::getBindAdapters(key == SettingsManager::BIND_ADDRESS6);
			for (const auto& adapter : bindAddresses) {
				ret["values"].push_back({
					{ "id", adapter.ip },
					{ "name", adapter.ip + (!adapter.adapterName.empty() ? " (" + adapter.adapterName + ")" : Util::emptyString) },
				});
			}
		} else if (key == SettingsManager::MAPPER) {
			auto mappers = ConnectivityManager::getInstance()->getMappers(false);
			for (const auto& mapper : mappers) {
				ret["values"].push_back({
					{ "id", mapper },
					{ "name", mapper }
				});
			}
		}

		return ret;
	}

	const string& CoreSettingItem::getTitle() const noexcept {
		return SettingItem::getDescription();
	}

	void CoreSettingItem::unset() noexcept {
		SettingItem::unset();
	}

	bool CoreSettingItem::setCurValue(const json& aJson) {
		if (isString(type)) {
			auto value = JsonUtil::parseValue<string>(name, aJson);
			if (type == TYPE_DIRECTORY_PATH) {
				value = Util::validatePath(value, true);
			}

			SettingsManager::getInstance()->set(static_cast<SettingsManager::StrSetting>(key), value);
		} else if (type == TYPE_NUMBER) {
			auto num = JsonUtil::parseValue<int>(name, aJson);

			auto minMax = minMaxMappings.find(key);
			if (minMax != minMaxMappings.end()) {
				JsonUtil::validateRange(name, num, (*minMax).second.min, (*minMax).second.max);
			}

			SettingsManager::getInstance()->set(static_cast<SettingsManager::IntSetting>(key), num);
		} else if (type == TYPE_BOOLEAN) {
			SettingsManager::getInstance()->set(static_cast<SettingsManager::BoolSetting>(key), JsonUtil::parseValue<bool>(name, aJson));
		} else {
			dcassert(0);
			return false;
		}

		return true;
	}
}
