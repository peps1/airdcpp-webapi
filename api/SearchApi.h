/*
* Copyright (C) 2011-2015 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_SEARCHAPI_H
#define DCPLUSPLUS_DCPP_SEARCHAPI_H

#include <web-server/stdinc.h>

#include <api/ApiModule.h>
#include <api/SearchResultInfo.h>
#include <api/common/ListViewController.h>

#include <client/typedefs.h>
#include <client/SearchManager.h>
#include <client/SearchQuery.h>

namespace webserver {
	class SearchApi : public ApiModule, private SearchManagerListener {
	public:
		SearchApi();
		~SearchApi();

		void onSocketRemoved() noexcept;

		int getVersion() const noexcept {
			return 0;
		}

		const PropertyList properties = {
			{ PROP_NAME, "name", TYPE_TEXT, SERIALIZE_TEXT, SORT_CUSTOM },
			{ PROP_RELEVANCY, "relevancy", TYPE_NUMERIC_OTHER, SERIALIZE_NUMERIC, SORT_NUMERIC },
			{ PROP_HITS, "hits", TYPE_NUMERIC_OTHER, SERIALIZE_NUMERIC, SORT_NUMERIC },
			{ PROP_USERS, "users", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_TEXT },
			{ PROP_TYPE, "type", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_CUSTOM },
			{ PROP_SIZE, "size", TYPE_SIZE, SERIALIZE_NUMERIC, SORT_NUMERIC },
			{ PROP_DATE, "time", TYPE_TIME, SERIALIZE_NUMERIC, SORT_NUMERIC },
			{ PROP_PATH, "path", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
			{ PROP_CONNECTION, "connection", TYPE_SPEED, SERIALIZE_NUMERIC, SORT_NUMERIC },
			{ PROP_SLOTS, "slots", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_CUSTOM },
			{ PROP_TTH, "tth", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
			{ PROP_IP, "ip", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_TEXT },
			{ PROP_DUPE, "dupe", TYPE_NUMERIC_OTHER, SERIALIZE_NUMERIC, SORT_NUMERIC },
		};

		enum Properties {
			PROP_TOKEN = -1,
			PROP_NAME,
			PROP_RELEVANCY,
			PROP_HITS,
			PROP_USERS,
			PROP_TYPE,
			PROP_SIZE,
			PROP_DATE,
			PROP_PATH,
			PROP_CONNECTION,
			PROP_SLOTS,
			PROP_TTH,
			PROP_IP,
			PROP_DUPE,
			PROP_LAST
		};
	private:
		SearchResultInfo::List getResultList();

		api_return SearchApi::handlePostSearch(ApiRequest& aRequest);
		api_return SearchApi::handleGetTypes(ApiRequest& aRequest);

		void on(SearchManagerListener::SR, const SearchResultPtr& aResult) noexcept;

		PropertyItemHandler<SearchResultInfoPtr> itemHandler;

		typedef ListViewController<SearchResultInfoPtr, PROP_LAST> SearchView;
		SearchView searchView;

		SearchResultInfo::Map results;
		shared_ptr<SearchQuery> curSearch;

		std::string  currentSearchToken;
		SharedMutex cs;
	};
}

#endif