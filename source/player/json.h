/*
 * BrewTube - A Homebrew YouTube App for the Wii
 * Copyright (C) 2026  ReviveMii Project
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef JSON_H
#define JSON_H

#include <string>
#include <vector>
#include <utility>

enum class JsonType { Null, Bool, Number, String, Array, Object };

class JsonValue
{
public:
	JsonType type = JsonType::Null;
	bool boolVal = false;
	double numVal = 0;
	std::string strVal;
	std::vector<JsonValue> arr;
	std::vector<std::pair<std::string, JsonValue>> obj;

	const JsonValue *get(const char *key) const;
	const JsonValue *at(size_t index) const;
	size_t size() const;
	const char *asString(const char *def = "") const;
	int asInt(int def = 0) const;
	bool asBool(bool def = false) const;
};

bool jsonParse(const char *text, size_t len, JsonValue &out);

#endif
