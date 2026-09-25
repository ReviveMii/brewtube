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

#ifndef PLAYER_H
#define PLAYER_H

enum PlayResult
{
	PLAY_DONE,
	PLAY_ERROR,
	PLAY_EXIT
};

struct YtResult;

PlayResult PlayFile(const char * path, char * err, int errSize, const char * customTitle = nullptr, const YtResult * ytMeta = nullptr);

#endif
