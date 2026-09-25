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

#include <stdlib.h>

#include "drivers/ogc/wii/WiiPlatform.h"
#include "drivers/Thread.h"
#include "libgui/Gui.h"
#include "menu.h"
#include "filelist.h"
#include "filebrowser.h"

static WiiPlatform platformInstance;
Platform * platform = &platformInstance;

#define IMAGE_DECODE_SCRATCH_SIZE ((640 * 480 * 4) + (480 * sizeof(void*)))

int main(int, char **)
{
	platform->init(640, 480);
	GuiImageData::setDecodeScratch(malloc(IMAGE_DECODE_SCRATCH_SIZE), IMAGE_DECODE_SCRATCH_SIZE);

	fontSystem = new GuiTextRenderer(font_ttf, font_ttf_size, platform->getVideo()->getGlyphRenderer(), platform->getVideo()->getUIScale());
	textTranslator = new GuiTextTranslator();
	textTranslator->loadLanguage(en_lang, en_lang_size);

	platform->getAudio()->start();

	InitDeviceCheckingThread();
	MainMenu();

	Thread::JoinAll();
	platform->requestExit();
}
