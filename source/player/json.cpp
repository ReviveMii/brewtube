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

#include <cstdlib>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>
#include <utility>

#include "json.h"

const JsonValue *JsonValue::get(const char *key) const
{
	if(type != JsonType::Object)
		return nullptr;

	for(const auto &kv : obj)
		if(kv.first == key)
			return &kv.second;

	return nullptr;
}

const JsonValue *JsonValue::at(size_t index) const
{
	if(type != JsonType::Array || index >= arr.size())
		return nullptr;

	return &arr[index];
}

size_t JsonValue::size() const
{
	return type == JsonType::Array ? arr.size() : (type == JsonType::Object ? obj.size() : 0);
}

const char *JsonValue::asString(const char *def) const
{
	return type == JsonType::String ? strVal.c_str() : def;
}

int JsonValue::asInt(int def) const
{
	return type == JsonType::Number ? (int)numVal : def;
}

bool JsonValue::asBool(bool def) const
{
	return type == JsonType::Bool ? boolVal : def;
}

namespace {

struct Parser
{
	const char *p;
	const char *end;

	void skipWs()
	{
		while(p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
			p++;
	}

	bool parseValue(JsonValue &out);

	bool parseString(std::string &out)
	{
		if(p >= end || *p != '"')
			return false;
		p++;

		while(p < end && *p != '"')
		{
			if(*p == '\\' && p + 1 < end)
			{
				p++;
				switch(*p)
				{
					case 'n': out += '\n'; break;
					case 't': out += '\t'; break;
					case 'r': out += '\r'; break;
					case 'b': out += '\b'; break;
					case 'f': out += '\f'; break;
					case '"': out += '"'; break;
					case '\\': out += '\\'; break;
					case '/': out += '/'; break;
					case 'u':
					{
						if(p + 4 >= end)
							return false;
						char hex[5] = { p[1], p[2], p[3], p[4], 0 };
						unsigned cp = strtoul(hex, nullptr, 16);
						p += 4;
						if(cp < 0x80)
							out += (char)cp;
						else if(cp < 0x800)
						{
							out += (char)(0xC0 | (cp >> 6));
							out += (char)(0x80 | (cp & 0x3F));
						}
						else
						{
							out += (char)(0xE0 | (cp >> 12));
							out += (char)(0x80 | ((cp >> 6) & 0x3F));
							out += (char)(0x80 | (cp & 0x3F));
						}
						break;
					}
					default: out += *p; break;
				}
				p++;
			}
			else
				out += *p++;
		}

		if(p >= end)
			return false;
		p++;
		return true;
	}

	bool parseArray(JsonValue &out)
	{
		out.type = JsonType::Array;
		p++;
		skipWs();
		if(p < end && *p == ']')
		{
			p++;
			return true;
		}

		for(;;)
		{
			JsonValue v;
			skipWs();
			if(!parseValue(v))
				return false;
			out.arr.push_back(std::move(v));
			skipWs();
			if(p >= end)
				return false;
			if(*p == ',')
			{
				p++;
				continue;
			}
			if(*p == ']')
			{
				p++;
				return true;
			}
			return false;
		}
	}

	bool parseObject(JsonValue &out)
	{
		out.type = JsonType::Object;
		p++;
		skipWs();
		if(p < end && *p == '}')
		{
			p++;
			return true;
		}

		for(;;)
		{
			skipWs();
			std::string key;
			if(p >= end || *p != '"' || !parseString(key))
				return false;
			skipWs();
			if(p >= end || *p != ':')
				return false;
			p++;
			skipWs();

			JsonValue v;
			if(!parseValue(v))
				return false;
			out.obj.emplace_back(std::move(key), std::move(v));

			skipWs();
			if(p >= end)
				return false;
			if(*p == ',')
			{
				p++;
				continue;
			}
			if(*p == '}')
			{
				p++;
				return true;
			}
			return false;
		}
	}
};

bool Parser::parseValue(JsonValue &out)
{
	skipWs();
	if(p >= end)
		return false;

	switch(*p)
	{
		case '{': return parseObject(out);
		case '[': return parseArray(out);
		case '"': out.type = JsonType::String; return parseString(out.strVal);
		case 't':
			if(end - p >= 4 && strncmp(p, "true", 4) == 0) { p += 4; out.type = JsonType::Bool; out.boolVal = true; return true; }
			return false;
		case 'f':
			if(end - p >= 5 && strncmp(p, "false", 5) == 0) { p += 5; out.type = JsonType::Bool; out.boolVal = false; return true; }
			return false;
		case 'n':
			if(end - p >= 4 && strncmp(p, "null", 4) == 0) { p += 4; out.type = JsonType::Null; return true; }
			return false;
		default:
		{
			const char *start = p;
			if(*p == '-')
				p++;
			while(p < end && (isdigit((unsigned char)*p) || *p == '.' || *p == 'e' || *p == 'E' || *p == '+' || *p == '-'))
				p++;
			if(p == start)
				return false;
			out.type = JsonType::Number;
			out.numVal = strtod(start, nullptr);
			return true;
		}
	}
}

}

bool jsonParse(const char *text, size_t len, JsonValue &out)
{
	Parser parser{ text, text + len };
	return parser.parseValue(out);
}
