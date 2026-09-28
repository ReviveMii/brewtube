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

#ifndef _MENU_H_
#define _MENU_H_

enum
{
	MENU_EXIT = -1,
	MENU_NONE,
	MENU_BROWSE
};

void MainMenu();
void MenuChannel(const char * channelIdOrHandle, const char * channelTitle = nullptr, const char * avatarUrl = nullptr);
void MenuPlaylist(const char * playlistId, const char * playlistTitle = nullptr);
void MenuSubscriptions();
void MenuLocalPlaylists();
void DrawPointers();
int WindowPrompt(const char * title, const char * msg, const char * btn1Label, const char * btn2Label);
bool RunWithLoadingScreen(const char * title, const char * msg, volatile bool & done);
bool EnterTextPrompt(const char * title, const char * okLabel, char * buf, int maxLen, const char * initialText = nullptr);

#endif
