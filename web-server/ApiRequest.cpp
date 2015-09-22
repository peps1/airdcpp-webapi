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

#include <web-server/stdinc.h>
#include <web-server/ApiRequest.h>

#include <client/StringTokenizer.h>
#include <client/Util.h>

namespace webserver {
	ApiRequest::ApiRequest(const string& aUrl, const string& aMethod, json& output_, string& error_) noexcept : responseJson(output_), responseError(error_) {
		parameters = StringTokenizer<std::string, deque>(aUrl, '/').getTokens();

		if (aMethod == "GET") {
			method = METHOD_GET;
		}
		else if (aMethod == "POST") {
			method = METHOD_POST;
		}
		else if (aMethod == "PUT") {
			method = METHOD_PUT;
		}
		else if (aMethod == "DELETE") {
			method = METHOD_DELETE;
		}
		else if (aMethod == "PATCH") {
			method = METHOD_PATCH;
		}
	}

	void ApiRequest::parseHttpRequestJson(const string& aRequestBody) {
		if (!aRequestBody.empty())
			requestJson = json::parse(aRequestBody);
	}

	void ApiRequest::parseSocketRequestJson(const json& aJson) {
		auto data = aJson.find("data");
		if (data != aJson.end()) {
			requestJson = *data;
		}
	}

	bool ApiRequest::validate(bool aExistingSocket, string& output_) {
		// Method
		if (method == METHOD_LAST) {
			output_ = "Unsupported method";
			return false;
		}

		// Module, version and command are always mandatory
		if (static_cast<int>(parameters.size()) < (aExistingSocket ? 1 : 3)) {
			output_ = "Not enough parameters";
			return false;
		}

		if (!aExistingSocket) {
			// API Module
			apiModule = parameters.front();
			parameters.pop_front();

			// Version
			auto version = parameters.front();
			parameters.pop_front();

			if (version.size() < 2) {
				output_ = "Invalid version";
				return false;
			}

			apiVersion = Util::toInt(version.substr(1));
		}

		// API Module section
		apiSection = parameters.front();
		parameters.pop_front();
		return true;
	}

	uint32_t ApiRequest::getTokenParam(int pos) const noexcept {
		return Util::toUInt32(parameters[pos]);
	}

	const string& ApiRequest::getStringParam(int pos) const noexcept {
		return parameters[pos];
	}

	int ApiRequest::getRangeParam(int pos) const noexcept {
		return Util::toInt(parameters[pos]);
	}
}