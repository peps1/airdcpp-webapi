/*
* Copyright (C) 2011-2018 AirDC++ Project
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

#include <api/FavoriteHubApi.h>

#include <web-server/JsonUtil.h>

#include <airdcpp/AirUtil.h>
#include <airdcpp/FavoriteManager.h>
#include <airdcpp/ShareManager.h>


namespace webserver {
	FavoriteHubApi::FavoriteHubApi(Session* aSession) : SubscribableApiModule(aSession, Access::FAVORITE_HUBS_VIEW),
		view("favorite_hub_view", this, FavoriteHubUtils::propertyHandler, getEntryList) {

		FavoriteManager::getInstance()->addListener(this);

		METHOD_HANDLER(Access::FAVORITE_HUBS_VIEW, METHOD_GET,		(RANGE_START_PARAM, RANGE_MAX_PARAM),	FavoriteHubApi::handleGetHubs);
		METHOD_HANDLER(Access::FAVORITE_HUBS_EDIT, METHOD_POST,		(),										FavoriteHubApi::handleAddHub);
		METHOD_HANDLER(Access::FAVORITE_HUBS_EDIT, METHOD_DELETE,	(TOKEN_PARAM),							FavoriteHubApi::handleRemoveHub);
		METHOD_HANDLER(Access::FAVORITE_HUBS_EDIT, METHOD_PATCH,	(TOKEN_PARAM),							FavoriteHubApi::handleUpdateHub);
		METHOD_HANDLER(Access::FAVORITE_HUBS_VIEW, METHOD_GET,		(TOKEN_PARAM),							FavoriteHubApi::handleGetHub);

		createSubscription("favorite_hub_created");
		createSubscription("favorite_hub_updated");
		createSubscription("favorite_hub_removed");
	}

	FavoriteHubApi::~FavoriteHubApi() {
		FavoriteManager::getInstance()->removeListener(this);
	}

	FavoriteHubEntryList FavoriteHubApi::getEntryList() noexcept {
		return FavoriteManager::getInstance()->getFavoriteHubs();
	}

	optional<int> FavoriteHubApi::deserializeIntHubSetting(const string& aFieldName, const json& aJson) {
		auto p = aJson.find(aFieldName);
		if (p == aJson.end()) {
			return boost::none;
		}

		if ((*p).is_null()) {
			return HUB_SETTING_DEFAULT_INT;
		}

		return JsonUtil::parseValue<int>(aFieldName, *p);
	}

	api_return FavoriteHubApi::handleGetHubs(ApiRequest& aRequest) {
		auto j = Serializer::serializeItemList(aRequest.getRangeParam(START_POS), aRequest.getRangeParam(MAX_COUNT), FavoriteHubUtils::propertyHandler, getEntryList());
		aRequest.setResponseBody(j);

		return websocketpp::http::status_code::ok;
	}

	void FavoriteHubApi::updateProperties(FavoriteHubEntryPtr& aEntry, const json& j, bool aNewHub) {
		auto name = JsonUtil::getOptionalField<string>("name", j, aNewHub);

		auto server = JsonUtil::getOptionalField<string>("hub_url", j, aNewHub);
		if (server) {
			if (!FavoriteManager::getInstance()->isUnique(*server, aEntry->getToken())) {
				JsonUtil::throwError("hub_url", JsonUtil::ERROR_EXISTS, STRING(FAVORITE_HUB_ALREADY_EXISTS));
			}
		}

		auto shareProfileToken = deserializeIntHubSetting("share_profile", j);
		if (shareProfileToken && *shareProfileToken != HUB_SETTING_DEFAULT_INT) {
			if (!AirUtil::isAdcHub(!server ? aEntry->getServer() : *server) && *shareProfileToken != SETTING(DEFAULT_SP) && *shareProfileToken != SP_HIDDEN) {
				JsonUtil::throwError("share_profile", JsonUtil::ERROR_INVALID, "Share profiles can't be changed for NMDC hubs");
			}

			if (*shareProfileToken) {
				auto shareProfilePtr = ShareManager::getInstance()->getShareProfile(*shareProfileToken, false);
				if (!shareProfilePtr) {
					JsonUtil::throwError("share_profile", JsonUtil::ERROR_INVALID, "Invalid share profile");
				}
			}
		}

		// We have valid values
		if (name) {
			aEntry->setName(*name);
		}

		if (server) {
			aEntry->setServer(*server);
		}

		if (shareProfileToken) {
			aEntry->get(HubSettings::ShareProfile) = *shareProfileToken;
		}

		// Values that don't need to be validated
		for (const auto& i : j.items()) {
			auto key = i.key();
			if (key == "auto_connect") {
				aEntry->setAutoConnect(JsonUtil::parseValue<bool>("auto_connect", i.value()));
			} else if (key == "hub_description") {
				aEntry->setDescription(JsonUtil::parseValue<string>("hub_description", i.value()));
			} else if (key == "password") {
				aEntry->setPassword(JsonUtil::parseValue<string>("password", i.value()));
			} else if (key == "nick") {
				aEntry->get(HubSettings::Nick) = JsonUtil::parseValue<string>("nick", i.value());
			} else if (key == "user_description") {
				aEntry->get(HubSettings::Description) = JsonUtil::parseValue<string>("user_description", i.value());
			} else if (key == "nmdc_encoding") {
				aEntry->get(HubSettings::NmdcEncoding) = JsonUtil::parseValue<string>("nmdc_encoding", i.value());
			} else if (key == "connection_mode_v4") {
				aEntry->get(HubSettings::Connection) = *JsonUtil::getEnumField<int>("connection_mode_v4", i.value(), false, SettingsManager::INCOMING_DISABLED, SettingsManager::INCOMING_PASSIVE);
			} else if (key == "connection_mode_v6") {
				aEntry->get(HubSettings::Connection6) = *JsonUtil::getEnumField<int>("connection_mode_v6", i.value(), false, SettingsManager::INCOMING_DISABLED, SettingsManager::INCOMING_PASSIVE);
			} else if (key == "connection_ip_v4") {
				aEntry->get(HubSettings::UserIp) = JsonUtil::parseValue<string>("connection_ip_v4", i.value());
			} else if (key == "connection_ip_v6") {
				aEntry->get(HubSettings::UserIp6) = JsonUtil::parseValue<string>("connection_ip_v6", i.value());
			}
		}
	}

	api_return FavoriteHubApi::handleAddHub(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		FavoriteHubEntryPtr e = new FavoriteHubEntry();
		updateProperties(e, reqJson, true);

		FavoriteManager::getInstance()->addFavoriteHub(e);

		aRequest.setResponseBody(Serializer::serializeItem(e, FavoriteHubUtils::propertyHandler));
		return websocketpp::http::status_code::ok;
	}

	api_return FavoriteHubApi::handleRemoveHub(ApiRequest& aRequest) {
		auto token = aRequest.getTokenParam();
		if (!FavoriteManager::getInstance()->removeFavoriteHub(token)) {
			aRequest.setResponseErrorStr("Hub not found");
			return websocketpp::http::status_code::not_found;
		}

		return websocketpp::http::status_code::no_content;
	}

	api_return FavoriteHubApi::handleGetHub(ApiRequest& aRequest) {
		auto entry = FavoriteManager::getInstance()->getFavoriteHubEntry(aRequest.getTokenParam());
		if (!entry) {
			aRequest.setResponseErrorStr("Hub not found");
			return websocketpp::http::status_code::not_found;
		}

		aRequest.setResponseBody(Serializer::serializeItem(entry, FavoriteHubUtils::propertyHandler));
		return websocketpp::http::status_code::ok;
	}

	api_return FavoriteHubApi::handleUpdateHub(ApiRequest& aRequest) {
		auto e = FavoriteManager::getInstance()->getFavoriteHubEntry(aRequest.getTokenParam());
		if (!e) {
			aRequest.setResponseErrorStr("Hub not found");
			return websocketpp::http::status_code::not_found;
		}

		updateProperties(e, aRequest.getRequestBody(), false);
		FavoriteManager::getInstance()->onFavoriteHubUpdated(e);

		aRequest.setResponseBody(Serializer::serializeItem(e, FavoriteHubUtils::propertyHandler));
		return websocketpp::http::status_code::ok;
	}

	void FavoriteHubApi::on(FavoriteManagerListener::FavoriteHubAdded, const FavoriteHubEntryPtr& e)  noexcept {
		view.onItemAdded(e);

		maybeSend("favorite_hub_created", [&] {
			return Serializer::serializeItem(e, FavoriteHubUtils::propertyHandler);
		});
	}
	void FavoriteHubApi::on(FavoriteManagerListener::FavoriteHubRemoved, const FavoriteHubEntryPtr& e) noexcept {
		view.onItemRemoved(e);

		maybeSend("favorite_hub_removed", [&] {
			return Serializer::serializeItem(e, FavoriteHubUtils::propertyHandler);
		});
	}
	void FavoriteHubApi::on(FavoriteManagerListener::FavoriteHubUpdated, const FavoriteHubEntryPtr& e) noexcept {
		view.onItemUpdated(e, toPropertyIdSet(FavoriteHubUtils::properties));

		maybeSend("favorite_hub_updated", [&] {
			return Serializer::serializeItem(e, FavoriteHubUtils::propertyHandler);
		});
	}
}