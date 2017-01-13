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

#include <api/ShareRootApi.h>
#include <api/common/Serializer.h>
#include <api/common/Deserializer.h>

#include <web-server/JsonUtil.h>

#include <airdcpp/AirUtil.h>
#include <airdcpp/HashManager.h>
#include <airdcpp/ShareManager.h>

namespace webserver {
	ShareRootApi::ShareRootApi(Session* aSession) : 
		SubscribableApiModule(aSession, Access::SETTINGS_VIEW),
		roots(ShareManager::getInstance()->getRootInfos()),
		rootView("share_root_view", this, ShareUtils::propertyHandler, std::bind(&ShareRootApi::getRoots, this)),
		timer(getTimer([this] { onTimer(); }, 5000)) {

		METHOD_HANDLER("roots", Access::SETTINGS_VIEW, ApiRequest::METHOD_GET, (), false, ShareRootApi::handleGetRoots);

		METHOD_HANDLER("root", Access::SETTINGS_EDIT, ApiRequest::METHOD_POST, (), true, ShareRootApi::handleAddRoot);
		METHOD_HANDLER("root", Access::SETTINGS_VIEW, ApiRequest::METHOD_GET, (TTH_PARAM), false, ShareRootApi::handleGetRoot);
		METHOD_HANDLER("root", Access::SETTINGS_EDIT, ApiRequest::METHOD_PATCH, (TTH_PARAM), true, ShareRootApi::handleUpdateRoot);
		METHOD_HANDLER("root", Access::SETTINGS_EDIT, ApiRequest::METHOD_DELETE, (TTH_PARAM), false, ShareRootApi::handleRemoveRoot);

		createSubscription("share_root_created");
		createSubscription("share_root_updated");
		createSubscription("share_root_removed");

		ShareManager::getInstance()->addListener(this);
		HashManager::getInstance()->addListener(this);
		timer->start(false);
	}

	ShareRootApi::~ShareRootApi() {
		timer->stop(true);
		HashManager::getInstance()->removeListener(this);
		ShareManager::getInstance()->removeListener(this);
	}

	ShareDirectoryInfoList ShareRootApi::getRoots() const noexcept {
		RLock l(cs);
		return roots;
	}

	api_return ShareRootApi::handleGetRoot(ApiRequest& aRequest) {
		auto info = getRoot(aRequest);
		aRequest.setResponseBody(Serializer::serializeItem(info, ShareUtils::propertyHandler));
		return websocketpp::http::status_code::ok;
	}

	api_return ShareRootApi::handleGetRoots(ApiRequest& aRequest) {
		auto j = Serializer::serializeItemList(ShareUtils::propertyHandler, ShareManager::getInstance()->getRootInfos());
		aRequest.setResponseBody(j);
		return websocketpp::http::status_code::ok;
	}

	api_return ShareRootApi::handleAddRoot(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto path = Util::validatePath(JsonUtil::getField<string>("path", reqJson, false), true);

		// Validate the path
		try {
			ShareManager::getInstance()->validateRootPath(path);
		} catch (ShareException& e) {
			JsonUtil::throwError("path", JsonUtil::ERROR_INVALID, e.what());
		}

		if (ShareManager::getInstance()->isRealPathShared(path)) {
			JsonUtil::throwError("path", JsonUtil::ERROR_INVALID, "Path is shared already");
		}

		auto info = std::make_shared<ShareDirectoryInfo>(path);

		parseRoot(info, reqJson, true);

		ShareManager::getInstance()->addRootDirectory(info);

		aRequest.setResponseBody(Serializer::serializeItem(info, ShareUtils::propertyHandler));
		return websocketpp::http::status_code::ok;
	}

	api_return ShareRootApi::handleUpdateRoot(ApiRequest& aRequest) {
		auto info = getRoot(aRequest);

		parseRoot(info, aRequest.getRequestBody(), false);
		ShareManager::getInstance()->updateRootDirectory(info);

		aRequest.setResponseBody(Serializer::serializeItem(info, ShareUtils::propertyHandler));
		return websocketpp::http::status_code::ok;
	}

	api_return ShareRootApi::handleRemoveRoot(ApiRequest& aRequest) {
		auto info = getRoot(aRequest);
		ShareManager::getInstance()->removeRootDirectory(info->path);
		return websocketpp::http::status_code::no_content;
	}

	void ShareRootApi::on(ShareManagerListener::RootCreated, const string& aPath) noexcept {
		auto info = ShareManager::getInstance()->getRootInfo(aPath);

		{
			WLock l(cs);
			roots.push_back(info);
		}

		rootView.onItemAdded(info);

		maybeSend("share_root_created", [&] { 
			return Serializer::serializeItem(info, ShareUtils::propertyHandler); 
		});
	}

	void ShareRootApi::on(ShareManagerListener::RootUpdated, const string& aPath) noexcept {
		auto info = ShareManager::getInstance()->getRootInfo(aPath);
		if (!info) {
			dcassert(0);
			return;
		}

		auto localInfo = findRoot(aPath);
		if (!localInfo) {
			dcassert(0);
			return;
		}

		localInfo->merge(info);
		info = localInfo;  // We need to use the same pointer because of listview

		onRootUpdated(localInfo, toPropertyIdSet(ShareUtils::properties));
	}

	void ShareRootApi::onRootUpdated(const ShareDirectoryInfoPtr& aInfo, PropertyIdSet&& aUpdatedProperties) noexcept {
		maybeSend("share_root_updated", [&] { 
			// Always serialize the full item
			return Serializer::serializeItem(aInfo, ShareUtils::propertyHandler);
		});

		//dcassert(rootView.hasSourceItem(aInfo));
		rootView.onItemUpdated(aInfo, aUpdatedProperties);
	}

	void ShareRootApi::on(ShareManagerListener::RootRemoved, const string& aPath) noexcept {
		ShareDirectoryInfoPtr info = nullptr;
		if (!rootView.isActive() && !subscriptionActive("share_root_removed")) {
			return;
		}

		auto root = findRoot(aPath);
		if (!root) {
			dcassert(0);
			return;
		}

		rootView.onItemRemoved(root);

		{
			WLock l(cs);
			roots.erase(remove(roots.begin(), roots.end(), root), roots.end());
		}

		maybeSend("share_root_removed", [&] {
			return Serializer::serializeItem(info, ShareUtils::propertyHandler);
		});
	}

	ShareDirectoryInfoPtr ShareRootApi::getRoot(const ApiRequest& aRequest) {
		RLock l(cs);
		auto i = boost::find_if(roots, ShareDirectoryInfo::IdCompare(Deserializer::parseTTH(aRequest.getStringParam(0))));
		if (i == roots.end()) {
			throw RequestException(websocketpp::http::status_code::not_found, "Root not found");
		}

		return *i;
	}

	ShareDirectoryInfoPtr ShareRootApi::findRoot(const string& aPath) noexcept {
		RLock l(cs);
		auto i = boost::find_if(roots, ShareDirectoryInfo::PathCompare(aPath));
		if (i == roots.end()) {
			return nullptr;
		}

		return *i;
	}

	void ShareRootApi::parseRoot(ShareDirectoryInfoPtr& aInfo, const json& j, bool aIsNew) {
		auto virtualName = JsonUtil::getOptionalField<string>("virtual_name", j, false);
		if (virtualName) {
			aInfo->virtualName = *virtualName;
		}

		auto profiles = JsonUtil::getOptionalField<ProfileTokenSet>("profiles", j, false);
		if (profiles) {
			// Only validate added profiles
			ProfileTokenSet diff;

			auto newProfiles = *profiles;
			for (const auto& p : newProfiles) {
				if (!ShareManager::getInstance()->getShareProfile(p)) {
					JsonUtil::throwError("profiles", JsonUtil::ERROR_INVALID, "Share profile " +  Util::toString(p)  + " was not found");
				}
			}


			std::set_difference(newProfiles.begin(), newProfiles.end(),
				aInfo->profiles.begin(), aInfo->profiles.end(), std::inserter(diff, diff.begin()));

			try {
				ShareManager::getInstance()->validateNewRootProfiles(aInfo->path, diff);
			} catch (ShareException& e) {
				JsonUtil::throwError(aIsNew ? "path" : "profiles", JsonUtil::ERROR_INVALID, e.what());
			}

			aInfo->profiles = newProfiles;
		}

		auto incoming = JsonUtil::getOptionalField<bool>("incoming", j, false);
		if (incoming) {
			aInfo->incoming = *incoming;
		}
	}

	// Show updates for roots that are being hashed regularly
	void ShareRootApi::onTimer() noexcept {
		ShareDirectoryInfoSet updatedRoots;

		{
			RLock l(cs);
			if (hashedPaths.empty()) {
				return;
			}

			for (const auto& p : hashedPaths) {
				auto i = boost::find_if(roots, [&](const ShareDirectoryInfoPtr& aInfo) {
					return AirUtil::isParentOrExactLocal(aInfo->path, p);
				});

				if (i != roots.end()) {
					updatedRoots.insert(*i);
				}
			} 
		}

		{
			WLock l(cs);
			hashedPaths.clear();
		}

		for (const auto& root : updatedRoots) {
			// Update with the new information
			auto newInfo = ShareManager::getInstance()->getRootInfo(root->path);
			if (newInfo) {
				WLock l(cs);
				root->merge(newInfo);
				onRootUpdated(root, { ShareUtils::PROP_SIZE, ShareUtils::PROP_TYPE });
			}
		}
	}

	void ShareRootApi::on(HashManagerListener::FileHashed, const string& aFilePath, HashedFile& aFileInfo) noexcept {
		WLock l(cs);
		hashedPaths.insert(Util::getFilePath(aFilePath));
	}
}