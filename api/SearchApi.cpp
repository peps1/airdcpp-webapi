/*
* Copyright (C) 2011-2016 AirDC++ Project
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

#include <api/SearchApi.h>

#include <api/common/Deserializer.h>
#include <api/common/FileSearchParser.h>

#include <airdcpp/BundleInfo.h>
#include <airdcpp/ClientManager.h>
#include <airdcpp/DirectSearch.h>
#include <airdcpp/SearchInstance.h>
#include <airdcpp/SearchManager.h>
#include <airdcpp/ShareManager.h>


#define DEFAULT_INSTANCE_EXPIRATION_MINUTES 30

namespace webserver {
	StringList SearchApi::subscriptionList = {

	};

	SearchApi::SearchApi(Session* aSession) : 
		ParentApiModule("instance", TOKEN_PARAM, Access::SEARCH, aSession, subscriptionList, SearchEntity::subscriptionList,
			[](const string& aId) { return Util::toUInt32(aId); },
			[](const SearchEntity& aInfo) { return serializeSearchInstance(aInfo); }
		),
		timer(getTimer([this] { onTimer(); }, 30 * 1000)) {

		METHOD_HANDLER("instance", Access::SEARCH, ApiRequest::METHOD_POST, (), false, SearchApi::handleCreateInstance);
		METHOD_HANDLER("instance", Access::SEARCH, ApiRequest::METHOD_DELETE, (TOKEN_PARAM), false, SearchApi::handleDeleteInstance);

		METHOD_HANDLER("types", Access::ANY, ApiRequest::METHOD_GET, (), false, SearchApi::handleGetTypes);

		// Create an initial search instance
		if (aSession->getSessionType() != Session::TYPE_BASIC_AUTH) {
			createInstance(0);
		}

		timer->start(false);
	}

	SearchApi::~SearchApi() {
		timer->stop(true);
	}

	void SearchApi::onTimer() noexcept {
		vector<SearchInstanceToken> expiredIds;
		forEachSubModule([&](const SearchEntity& aInstance) {
			if (aInstance.getExpirationTick() > 0 && GET_TICK() > aInstance.getExpirationTick()) {
				expiredIds.push_back(aInstance.getId());
				dcdebug("Removing an expired search instance (expiration: " U64_FMT ", now: " U64_FMT ")\n", aInstance.getExpirationTick(), GET_TICK());
			}
		});

		for (const auto& id : expiredIds) {
			removeSubModule(id);
		}
	}

	json SearchApi::serializeSearchInstance(const SearchEntity& aSearch) noexcept {
		return {
			{ "id", aSearch.getId() },
			{ "expiration_minutes", static_cast<int64_t>(aSearch.getExpirationTick()) - static_cast<int64_t>(GET_TICK()) },
		};
	}

	SearchEntity::Ptr SearchApi::createInstance(uint64_t aExpirationTick) {
		auto id = instanceIdCounter++;
		auto module = std::make_shared<SearchEntity>(this, make_shared<SearchInstance>(), id, aExpirationTick);

		addSubModule(id, module);
		return module;
	}

	api_return SearchApi::handleCreateInstance(ApiRequest& aRequest) {
		auto expirationMinutes = JsonUtil::getOptionalFieldDefault<int>("expiration", aRequest.getRequestBody(), DEFAULT_INSTANCE_EXPIRATION_MINUTES);

		auto instance = createInstance(GET_TICK() + expirationMinutes * 60 * 1000);

		aRequest.setResponseBody(serializeSearchInstance(*instance.get()));
		return websocketpp::http::status_code::ok;
	}

	api_return SearchApi::handleDeleteInstance(ApiRequest& aRequest) {
		auto instance = getSubModule(aRequest);
		removeSubModule(instance->getId());

		return websocketpp::http::status_code::no_content;
	}

	api_return SearchApi::handleGetTypes(ApiRequest& aRequest) {
		auto getName = [](const string& aId) -> string {
			if (SearchManager::isDefaultTypeStr(aId)) {
				return string(SearchManager::getTypeStr(aId[0] - '0'));
			}

			return aId;
		};

		auto types = SearchManager::getInstance()->getSearchTypes();

		json retJ;
		for (const auto& s : types) {
			retJ.push_back({
				{ "id", Serializer::getFileTypeId(s.first) },
				{ "str", getName(s.first) },
				{ "extensions", s.second },
				{ "default_type", SearchManager::isDefaultTypeStr(s.first) }
			});
		}

		aRequest.setResponseBody(retJ);
		return websocketpp::http::status_code::ok;
	}
}