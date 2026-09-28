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

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <memory>

#include "libgui/Gui.h"
#include "drivers/Platform.h"
#include "menu.h"
#include "player.h"
#include "filelist.h"
#include "filebrowser.h"
#include "youtube.h"
#include "netstream.h"

static GuiImageData * pointer[4];
static GuiWindow * mainWindow = nullptr;

void DrawPointers()
{
	for(int i = 3; i >= 0; i--)
	{
		const InputPadData & pad = controller[i]->getPadData();
		if(pad.validPointer)
			platform->getVideo()->getImageRenderer()->drawTexture(pointer[i]->getTexture(), pad.cursor_x - 48, pad.cursor_y - 48, 96, 96, pad.cursor_angle, 1, 1, 255);
	}
}

static bool UpdateGui()
{
	platform->getInput()->update();

	for(int i = 3; i >= 0; i--)
		mainWindow->update(controller[i]);

	mainWindow->draw();
	DrawPointers();
	platform->getVideo()->render();

	if(platform->shouldExit())
	{
		for(int i = 0; i <= 255; i += 15)
		{
			mainWindow->draw();
			platform->getVideo()->getImageRenderer()->drawRectangle(0, 0, platform->getVideo()->getScreenWidth(), platform->getVideo()->getScreenHeight(), (PixelColor){0, 0, 0, (uint8_t)i});
			platform->getVideo()->render();
		}
		return false;
	}

	return true;
}

int WindowPrompt(const char *title, const char *msg, const char *btn1Label, const char *btn2Label)
{
	int choice = -1;

	GuiWindow promptWindow(448, 250);
	promptWindow.setAlignment(ALIGN_H::CENTRE, ALIGN_V::MIDDLE);
	promptWindow.setPosition(0, -10);

	GuiSound btnSoundOver(button_over_pcm, button_over_pcm_size, SOUND::PCM);
	GuiImageData btnOutline(button_png);
	GuiImageData btnOutlineOver(button_over_png);
	GuiTrigger trigA;
	trigA.setPrimaryTrigger();

	GuiImage bg(448, 250, (PixelColor){22, 25, 36, 250});
	GuiImage border(448, 3, (PixelColor){0, 120, 215, 255});
	border.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);

	GuiText titleTxt(title, 24, (PixelColor){255, 255, 255, 255});
	titleTxt.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	titleTxt.setPosition(0, 28);

	GuiText msgTxt(msg, 20, (PixelColor){180, 185, 200, 255});
	msgTxt.setAlignment(ALIGN_H::CENTRE, ALIGN_V::MIDDLE);
	msgTxt.setPosition(0, -10);
	msgTxt.setWrap(true, 400);

	const PixelColor btnText = {25, 28, 38, 255};
	GuiText btn1Txt(btn1Label, 20, btnText);
	GuiImage btn1Img(&btnOutline);
	GuiImage btn1ImgOver(&btnOutlineOver);
	GuiButton btn1(btnOutline.getWidth(), btnOutline.getHeight());

	if(btn2Label)
	{
		btn1.setAlignment(ALIGN_H::LEFT, ALIGN_V::BOTTOM);
		btn1.setPosition(25, -25);
	}
	else
	{
		btn1.setAlignment(ALIGN_H::CENTRE, ALIGN_V::BOTTOM);
		btn1.setPosition(0, -25);
	}

	btn1.setLabel(&btn1Txt);
	btn1.setImage(&btn1Img);
	btn1.setImageOver(&btn1ImgOver);
	btn1.setSoundOver(&btnSoundOver);
	btn1.setTrigger(&trigA);
	btn1.setState(STATE::SELECTED);
	btn1.setEffectGrow();

	GuiText btn2Txt(btn2Label ? btn2Label : "", 20, btnText);
	GuiImage btn2Img(&btnOutline);
	GuiImage btn2ImgOver(&btnOutlineOver);
	GuiButton btn2(btnOutline.getWidth(), btnOutline.getHeight());
	btn2.setAlignment(ALIGN_H::RIGHT, ALIGN_V::BOTTOM);
	btn2.setPosition(-25, -25);
	btn2.setLabel(&btn2Txt);
	btn2.setImage(&btn2Img);
	btn2.setImageOver(&btn2ImgOver);
	btn2.setSoundOver(&btnSoundOver);
	btn2.setTrigger(&trigA);
	btn2.setEffectGrow();

	promptWindow.append(&bg);
	promptWindow.append(&border);
	promptWindow.append(&titleTxt);
	promptWindow.append(&msgTxt);
	promptWindow.append(&btn1);

	if(btn2Label)
		promptWindow.append(&btn2);

	promptWindow.setEffect(EFFECT::SLIDE_TOP | EFFECT::SLIDE_IN, 50);
	mainWindow->setState(STATE::DISABLED);
	mainWindow->appendWithAutoRemove(&promptWindow);
	mainWindow->changeFocus(&promptWindow);

	while(choice == -1)
	{
		if(!UpdateGui()) return -1;

		if(btn1.getState() == STATE::CLICKED)
			choice = 1;
		else if(btn2.getState() == STATE::CLICKED)
			choice = 0;
	}

	promptWindow.setEffect(EFFECT::SLIDE_TOP | EFFECT::SLIDE_OUT, 50);
	while(promptWindow.getEffect() > 0)
	{
		if(!UpdateGui()) return choice;
	}

	mainWindow->setState(STATE::DEFAULT);
	return choice;
}

bool RunWithLoadingScreen(const char * title, const char * msg, volatile bool & done)
{
	VideoDriver * video = platform->getVideo();
	int sw = video->getScreenWidth();
	int sh = video->getScreenHeight();
	int winW = 380, winH = 130;
	int winX = (sw - winW) / 2;
	int winY = (sh - winH) / 2;

	GuiWindow win(winW, winH);
	win.setPosition(winX, winY);
	win.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);

	GuiImage bg(winW, winH, (PixelColor){22, 25, 36, 250});
	GuiImage border(winW, 3, (PixelColor){0, 120, 215, 255});
	border.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);

	GuiText titleTxt(title, 22, (PixelColor){255, 255, 255, 255});
	titleTxt.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	titleTxt.setPosition(0, 20);

	char animMsg[128];
	GuiText msgTxt(msg, 18, (PixelColor){180, 185, 200, 255});
	msgTxt.setAlignment(ALIGN_H::CENTRE, ALIGN_V::MIDDLE);
	msgTxt.setPosition(0, 18);

	win.append(&bg);
	win.append(&border);
	win.append(&titleTxt);
	win.append(&msgTxt);

	int frame = 0;
	while(!done && !platform->shouldExit())
	{
		platform->getInput()->update();

		const char * dots = (frame % 60 < 15) ? "" : ((frame % 60 < 30) ? "." : ((frame % 60 < 45) ? ".." : "..."));
		snprintf(animMsg, sizeof(animMsg), "%s%s", msg, dots);
		msgTxt.setText(animMsg);

		video->getImageRenderer()->drawRectangle(0, 0, sw, sh, (PixelColor){0, 0, 0, 160});
		win.draw();
		DrawPointers();
		video->render();

		frame++;
		usleep(16000);
	}

	return !platform->shouldExit();
}

bool EnterTextPrompt(const char * promptTitle, const char * okLabel, char * buf, int maxLen, const char * initialText)
{
	static const char * const kbRow0 = "1234567890";
	static const char * const kbRow1 = "qwertyuiop";
	static const char * const kbRow2 = "asdfghjkl";
	static const char * const kbRow3 = "zxcvbnm./:-_";
	const int MAX_KEYS = 50;
	const int KEY_W = 44, KEY_H = 38, KEY_GAP = 5;
	const PixelColor white = {255, 255, 255, 255};
	const PixelColor btnText = {25, 28, 38, 255};

	if(!buf || maxLen <= 0) return false;
	if(initialText && initialText[0])
	{
		snprintf(buf, maxLen, "%s", initialText);
	}
	else
	{
		buf[0] = '\0';
	}
	int len = strlen(buf);
	bool accepted = false;
	bool done = false;

	GuiWindow kbWindow(580, 390);
	kbWindow.setAlignment(ALIGN_H::CENTRE, ALIGN_V::MIDDLE);
	kbWindow.setPosition(0, -10);

	GuiImage kbBg(580, 390, (PixelColor){22, 25, 36, 250});
	GuiImage border(580, 3, (PixelColor){0, 120, 215, 255});
	border.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);

	GuiText titleTxt(promptTitle ? promptTitle : "Enter Text", 24, white);
	titleTxt.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	titleTxt.setPosition(0, 18);

	GuiImage barBg(520, 38, (PixelColor){32, 36, 48, 255});
	barBg.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	barBg.setPosition(0, 52);

	GuiText urlTxt(buf, 18, white);
	urlTxt.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	urlTxt.setPosition(35, 60);
	urlTxt.setMaxWidth(510);

	kbWindow.append(&kbBg);
	kbWindow.append(&border);
	kbWindow.append(&titleTxt);
	kbWindow.append(&barBg);
	kbWindow.append(&urlTxt);

	GuiImageData btnOutline(button_png);
	GuiImageData btnOutlineOver(button_over_png);
	GuiSound btnSoundOver(button_over_pcm, button_over_pcm_size, SOUND::PCM);
	GuiTrigger trigA;
	trigA.setPrimaryTrigger();

	GuiButton * keyBtn[MAX_KEYS] = { nullptr };
	GuiText * keyTxt[MAX_KEYS] = { nullptr };
	GuiImage * keyImg[MAX_KEYS] = { nullptr };
	GuiImage * keyImgOver[MAX_KEYS] = { nullptr };
	char keyChar[MAX_KEYS];
	int keyCount = 0;

	auto addRow = [&](const char * rowChars, int rowY)
	{
		int cols = strlen(rowChars);
		int rowWidth = cols * KEY_W + (cols - 1) * KEY_GAP;
		int startX = -rowWidth / 2 + KEY_W / 2;

		for(int c = 0; c < cols; c++)
		{
			int i = keyCount++;
			keyChar[i] = rowChars[c];
			char label[2] = { keyChar[i], '\0' };

			keyImg[i] = new GuiImage(&btnOutline);
			keyImg[i]->setSize(KEY_W, KEY_H);
			keyImgOver[i] = new GuiImage(&btnOutlineOver);
			keyImgOver[i]->setSize(KEY_W, KEY_H);
			keyTxt[i] = new GuiText(label, 19, btnText);

			keyBtn[i] = new GuiButton(KEY_W, KEY_H);
			keyBtn[i]->setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
			keyBtn[i]->setPosition(startX + c * (KEY_W + KEY_GAP), rowY);
			keyBtn[i]->setImage(keyImg[i]);
			keyBtn[i]->setImageOver(keyImgOver[i]);
			keyBtn[i]->setLabel(keyTxt[i]);
			keyBtn[i]->setSoundOver(&btnSoundOver);
			keyBtn[i]->setTrigger(&trigA);
			keyBtn[i]->setEffectGrow();

			kbWindow.append(keyBtn[i]);
		}
	};

	addRow(kbRow0, 102);
	addRow(kbRow1, 148);
	addRow(kbRow2, 194);
	addRow(kbRow3, 240);

	int barY = 288;

	GuiImage backImg(&btnOutline);
	backImg.setSize(80, KEY_H);
	GuiImage backImgOver(&btnOutlineOver);
	backImgOver.setSize(80, KEY_H);
	GuiText backTxt("Del", 18, btnText);
	GuiButton backBtn(80, KEY_H);
	backBtn.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	backBtn.setPosition(-140, barY);
	backBtn.setImage(&backImg);
	backBtn.setImageOver(&backImgOver);
	backBtn.setLabel(&backTxt);
	backBtn.setSoundOver(&btnSoundOver);
	backBtn.setTrigger(&trigA);
	backBtn.setEffectGrow();

	GuiImage spaceImg(&btnOutline);
	spaceImg.setSize(180, KEY_H);
	GuiImage spaceImgOver(&btnOutlineOver);
	spaceImgOver.setSize(180, KEY_H);
	GuiText spaceTxt("Space", 18, btnText);
	GuiButton spaceBtn(180, KEY_H);
	spaceBtn.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	spaceBtn.setPosition(0, barY);
	spaceBtn.setImage(&spaceImg);
	spaceBtn.setImageOver(&spaceImgOver);
	spaceBtn.setLabel(&spaceTxt);
	spaceBtn.setSoundOver(&btnSoundOver);
	spaceBtn.setTrigger(&trigA);
	spaceBtn.setEffectGrow();

	GuiImage clearImg(&btnOutline);
	clearImg.setSize(80, KEY_H);
	GuiImage clearImgOver(&btnOutlineOver);
	clearImgOver.setSize(80, KEY_H);
	GuiText clearTxt("Clear", 18, btnText);
	GuiButton clearBtn(80, KEY_H);
	clearBtn.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	clearBtn.setPosition(140, barY);
	clearBtn.setImage(&clearImg);
	clearBtn.setImageOver(&clearImgOver);
	clearBtn.setLabel(&clearTxt);
	clearBtn.setSoundOver(&btnSoundOver);
	clearBtn.setTrigger(&trigA);
	clearBtn.setEffectGrow();

	int actY = 336;

	GuiImage okImg(&btnOutline);
	okImg.setSize(150, KEY_H);
	GuiImage okImgOver(&btnOutlineOver);
	okImgOver.setSize(150, KEY_H);
	GuiText okTxt(okLabel ? okLabel : "OK", 20, btnText);
	GuiButton okBtn(150, KEY_H);
	okBtn.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	okBtn.setPosition(-90, actY);
	okBtn.setImage(&okImg);
	okBtn.setImageOver(&okImgOver);
	okBtn.setLabel(&okTxt);
	okBtn.setSoundOver(&btnSoundOver);
	okBtn.setTrigger(&trigA);
	okBtn.setEffectGrow();

	GuiImage cancelImg(&btnOutline);
	cancelImg.setSize(150, KEY_H);
	GuiImage cancelImgOver(&btnOutlineOver);
	cancelImgOver.setSize(150, KEY_H);
	GuiText cancelTxt("Cancel", 20, btnText);
	GuiButton cancelBtn(150, KEY_H);
	cancelBtn.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	cancelBtn.setPosition(90, actY);
	cancelBtn.setImage(&cancelImg);
	cancelBtn.setImageOver(&cancelImgOver);
	cancelBtn.setLabel(&cancelTxt);
	cancelBtn.setSoundOver(&btnSoundOver);
	cancelBtn.setTrigger(&trigA);
	cancelBtn.setEffectGrow();

	kbWindow.append(&backBtn);
	kbWindow.append(&spaceBtn);
	kbWindow.append(&clearBtn);
	kbWindow.append(&okBtn);
	kbWindow.append(&cancelBtn);

	kbWindow.setEffect(EFFECT::SLIDE_TOP | EFFECT::SLIDE_IN, 50);
	mainWindow->setState(STATE::DISABLED);
	mainWindow->appendWithAutoRemove(&kbWindow);
	mainWindow->changeFocus(&kbWindow);

	while(!done)
	{
		if(!UpdateGui())
			break;

		for(int i = 0; i < keyCount; i++)
		{
			if(keyBtn[i]->getState() == STATE::CLICKED)
			{
				keyBtn[i]->resetState();
				if(len < maxLen - 1)
				{
					buf[len++] = keyChar[i];
					buf[len] = '\0';
					urlTxt.setText(buf);
				}
			}
		}

		if(spaceBtn.getState() == STATE::CLICKED)
		{
			spaceBtn.resetState();
			if(len < maxLen - 1)
			{
				buf[len++] = ' ';
				buf[len] = '\0';
				urlTxt.setText(buf);
			}
		}

		if(backBtn.getState() == STATE::CLICKED)
		{
			backBtn.resetState();
			if(len > 0)
			{
				buf[--len] = '\0';
				urlTxt.setText(buf);
			}
		}

		if(clearBtn.getState() == STATE::CLICKED)
		{
			clearBtn.resetState();
			len = 0;
			buf[0] = '\0';
			urlTxt.setText(buf);
		}

		if(okBtn.getState() == STATE::CLICKED)
		{
			okBtn.resetState();
			if(len > 0)
			{
				accepted = true;
				done = true;
			}
		}

		if(cancelBtn.getState() == STATE::CLICKED)
		{
			cancelBtn.resetState();
			done = true;
		}

		uint32_t pressed = 0;
		for(int i = 0; i < 4; i++)
			pressed |= controller[i]->getPadData().buttons_d;
		if(pressed & INPUT_BTN_B)
		{
			if(len > 0)
			{
				buf[--len] = '\0';
				urlTxt.setText(buf);
			}
		}
	}

	kbWindow.setEffect(EFFECT::SLIDE_TOP | EFFECT::SLIDE_OUT, 50);
	while(kbWindow.getEffect() > 0)
	{
		if(!UpdateGui())
			break;
	}
	mainWindow->setState(STATE::DEFAULT);

	for(int i = 0; i < keyCount; i++)
	{
		delete keyBtn[i];
		delete keyTxt[i];
		delete keyImg[i];
		delete keyImgOver[i];
	}

	if(!accepted)
		buf[0] = '\0';

	return accepted;
}

static bool EnterUrlPrompt(char * url, int urlSize, const char * promptTitle = "Enter Media URL")
{
	return EnterTextPrompt(promptTitle, "Play", url, urlSize, nullptr);
}

static int ShowOtherMenuPrompt()
{
	int choice = -1;

	GuiWindow otherWin(460, 355);
	otherWin.setAlignment(ALIGN_H::CENTRE, ALIGN_V::MIDDLE);
	otherWin.setPosition(0, -10);

	GuiSound btnSoundOver(button_over_pcm, button_over_pcm_size, SOUND::PCM);
	GuiImageData btnOutline(button_png);
	GuiImageData btnOutlineOver(button_over_png);
	GuiTrigger trigA;
	trigA.setPrimaryTrigger();

	GuiImage bg(460, 390, (PixelColor){22, 25, 36, 250});
	GuiImage border(460, 3, (PixelColor){0, 120, 215, 255});
	border.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);

	GuiText titleTxt("Other / Settings", 24, (PixelColor){255, 255, 255, 255});
	titleTxt.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	titleTxt.setPosition(0, 16);

	const PixelColor btnText = {25, 28, 38, 255};
	GuiImage filesImg(&btnOutline);
	filesImg.setSize(290, 40);
	GuiImage filesImgOver(&btnOutlineOver);
	filesImgOver.setSize(290, 40);
	GuiText filesTxt("Local Storage (SD / USB)", 18, btnText);
	GuiButton filesBtn(290, 40);
	filesBtn.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	filesBtn.setPosition(0, 46);
	filesBtn.setImage(&filesImg);
	filesBtn.setImageOver(&filesImgOver);
	filesBtn.setLabel(&filesTxt);
	filesBtn.setSoundOver(&btnSoundOver);
	filesBtn.setTrigger(&trigA);
	filesBtn.setEffectGrow();

	GuiImage urlImg(&btnOutline);
	urlImg.setSize(290, 40);
	GuiImage urlImgOver(&btnOutlineOver);
	urlImgOver.setSize(290, 40);
	GuiText urlTxt("Direct Stream URL", 18, btnText);
	GuiButton urlBtn(290, 40);
	urlBtn.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	urlBtn.setPosition(0, 90);
	urlBtn.setImage(&urlImg);
	urlBtn.setImageOver(&urlImgOver);
	urlBtn.setLabel(&urlTxt);
	urlBtn.setSoundOver(&btnSoundOver);
	urlBtn.setTrigger(&trigA);
	urlBtn.setEffectGrow();

	GuiImage chanImg(&btnOutline);
	chanImg.setSize(290, 40);
	GuiImage chanImgOver(&btnOutlineOver);
	chanImgOver.setSize(290, 40);
	GuiText chanTxt("Subscriptions", 18, btnText);
	GuiButton chanBtn(290, 40);
	chanBtn.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	chanBtn.setPosition(0, 134);
	chanBtn.setImage(&chanImg);
	chanImgOver.setSize(290, 40);
	chanBtn.setImageOver(&chanImgOver);
	chanBtn.setLabel(&chanTxt);
	chanBtn.setSoundOver(&btnSoundOver);
	chanBtn.setTrigger(&trigA);
	chanBtn.setEffectGrow();

	GuiImage plImg(&btnOutline);
	plImg.setSize(290, 40);
	GuiImage plImgOver(&btnOutlineOver);
	plImgOver.setSize(290, 40);
	GuiText plTxt("Local Playlists", 18, btnText);
	GuiButton plBtn(290, 40);
	plBtn.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	plBtn.setPosition(0, 178);
	plBtn.setImage(&plImg);
	plBtn.setImageOver(&plImgOver);
	plBtn.setLabel(&plTxt);
	plBtn.setSoundOver(&btnSoundOver);
	plBtn.setTrigger(&trigA);
	plBtn.setEffectGrow();

	GuiImage clientImg(&btnOutline);
	clientImg.setSize(290, 40);
	GuiImage clientImgOver(&btnOutlineOver);
	clientImgOver.setSize(290, 40);
	char clientStr[48];
	snprintf(clientStr, sizeof(clientStr), "YTVideoClient: %s", ytGetClient() == YT_CLIENT_VISIONOS ? "VISIONOS" : "ANDROID");
	GuiText clientTxt(clientStr, 18, btnText);
	GuiButton clientBtn(290, 40);
	clientBtn.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	clientBtn.setPosition(0, 222);
	clientBtn.setImage(&clientImg);
	clientBtn.setImageOver(&clientImgOver);
	clientBtn.setLabel(&clientTxt);
	clientBtn.setSoundOver(&btnSoundOver);
	clientBtn.setTrigger(&trigA);
	clientBtn.setEffectGrow();

	GuiImage cancelImg(&btnOutline);
	cancelImg.setSize(140, 38);
	GuiImage cancelImgOver(&btnOutlineOver);
	cancelImgOver.setSize(140, 38);
	GuiText cancelTxt("Close", 19, btnText);
	GuiButton cancelBtn(140, 38);
	cancelBtn.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	cancelBtn.setPosition(0, 280);
	cancelBtn.setImage(&cancelImg);
	cancelBtn.setImageOver(&cancelImgOver);
	cancelBtn.setLabel(&cancelTxt);
	cancelBtn.setSoundOver(&btnSoundOver);
	cancelBtn.setTrigger(&trigA);
	cancelBtn.setEffectGrow();

	otherWin.append(&bg);
	otherWin.append(&border);
	otherWin.append(&titleTxt);
	otherWin.append(&filesBtn);
	otherWin.append(&urlBtn);
	otherWin.append(&chanBtn);
	otherWin.append(&plBtn);
	otherWin.append(&clientBtn);
	otherWin.append(&cancelBtn);

	otherWin.setEffect(EFFECT::SLIDE_TOP | EFFECT::SLIDE_IN, 50);
	mainWindow->setState(STATE::DISABLED);
	mainWindow->appendWithAutoRemove(&otherWin);
	mainWindow->changeFocus(&otherWin);

	while(choice == -1)
	{
		if(!UpdateGui()) return -1;

		if(filesBtn.getState() == STATE::CLICKED)
			choice = 1;
		else if(urlBtn.getState() == STATE::CLICKED)
			choice = 2;
		else if(chanBtn.getState() == STATE::CLICKED)
			choice = 3;
		else if(plBtn.getState() == STATE::CLICKED)
			choice = 4;
		else if(clientBtn.getState() == STATE::CLICKED)
		{
			clientBtn.resetState();
			ytSetClient(ytGetClient() == YT_CLIENT_ANDROID ? YT_CLIENT_VISIONOS : YT_CLIENT_ANDROID);
			snprintf(clientStr, sizeof(clientStr), "YTVideoClient: %s", ytGetClient() == YT_CLIENT_VISIONOS ? "VISIONOS" : "ANDROID");
			clientTxt.setText(clientStr);
		}
		else if(cancelBtn.getState() == STATE::CLICKED)
			choice = 0;

		uint32_t pressed = 0;
		for(int i = 0; i < 4; i++)
			pressed |= controller[i]->getPadData().buttons_d;
		if(pressed & INPUT_BTN_B)
			choice = 0;
	}

	otherWin.setEffect(EFFECT::SLIDE_TOP | EFFECT::SLIDE_OUT, 50);
	while(otherWin.getEffect() > 0)
	{
		if(!UpdateGui()) return choice;
	}

	mainWindow->setState(STATE::DEFAULT);
	return choice;
}

static int MenuBrowseFiles()
{
	if(ParseDeviceList() <= 0)
	{
		WindowPrompt("No Storage", "No storage device detected. Please insert an SD card or USB drive.", "OK", nullptr);
		return MENU_NONE;
	}

	int menu = MENU_NONE;
	const PixelColor white = {255, 255, 255, 255};
	const PixelColor grey = {170, 175, 190, 255};
	const PixelColor btnText = {25, 28, 38, 255};

	int screenWidth = platform->getVideo()->getScreenWidth();

	GuiImage headerBg(screenWidth, 84, (PixelColor){10, 12, 18, 255});
	headerBg.setAlpha(230);
	GuiImage headerLine(screenWidth, 3, (PixelColor){0, 120, 215, 255});
	headerLine.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	headerLine.setPosition(0, 84);

	GuiText titleTxt("BrewTube - Local Files", 28, white);
	titleTxt.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	titleTxt.setPosition(40, 14);

	GuiText pathTxt("", 18, grey);
	pathTxt.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	pathTxt.setPosition(40, 54);
	pathTxt.setMaxWidth(560);

	GuiTrigger trigA;
	trigA.setPrimaryTrigger();

	GuiFileBrowser fileBrowser(552, 248);
	fileBrowser.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	fileBrowser.setPosition(0, 105);

	GuiImageData btnOutline(button_png);
	GuiImageData btnOutlineOver(button_over_png);

	GuiText backBtnTxt("Back to BrewTube", 19, btnText);
	GuiImage backBtnImg(&btnOutline);
	backBtnImg.setSize(220, 48);
	GuiImage backBtnImgOver(&btnOutlineOver);
	backBtnImgOver.setSize(220, 48);
	GuiButton backBtn(220, 48);
	backBtn.setAlignment(ALIGN_H::LEFT, ALIGN_V::BOTTOM);
	backBtn.setPosition(40, -18);
	backBtn.setLabel(&backBtnTxt);
	backBtn.setImage(&backBtnImg);
	backBtn.setImageOver(&backBtnImgOver);
	backBtn.setTrigger(&trigA);
	backBtn.setEffectGrow();

	GuiText exitBtnTxt("Exit", 20, btnText);
	GuiImage exitBtnImg(&btnOutline);
	exitBtnImg.setSize(120, 48);
	GuiImage exitBtnImgOver(&btnOutlineOver);
	exitBtnImgOver.setSize(120, 48);
	GuiButton exitBtn(120, 48);
	exitBtn.setAlignment(ALIGN_H::RIGHT, ALIGN_V::BOTTOM);
	exitBtn.setPosition(-40, -18);
	exitBtn.setLabel(&exitBtnTxt);
	exitBtn.setImage(&exitBtnImg);
	exitBtn.setImageOver(&exitBtnImgOver);
	exitBtn.setTrigger(&trigA);
	exitBtn.setEffectGrow();

	GuiWindow browseWindow(screenWidth, platform->getVideo()->getScreenHeight());
	browseWindow.append(&headerBg);
	browseWindow.append(&headerLine);
	browseWindow.append(&titleTxt);
	browseWindow.append(&pathTxt);
	browseWindow.append(&backBtn);
	browseWindow.append(&exitBtn);

	mainWindow->appendWithAutoRemove(&fileBrowser);
	mainWindow->appendWithAutoRemove(&browseWindow);

	char lastPath[MAXPATHLEN + 130] = "\x01";

	auto enterFolder = [&]()
	{
		if(BrowserChangeFolder() > 0)
		{
			fileBrowser.resetState();
			fileBrowser.fileList[0]->setState(STATE::SELECTED);
			fileBrowser.triggerUpdate();
			return;
		}

		if(browserErrorMsg[0] != '\0')
		{
			WindowPrompt("Error", browserErrorMsg, "OK", nullptr);
			browserErrorMsg[0] = '\0';
		}
		menu = MENU_BROWSE;
	};

	while(menu == MENU_NONE)
	{
		if(!UpdateGui())
			return MENU_EXIT;

		if(browserDeviceListChanged)
		{
			browserDeviceListChanged = false;
			if(ParseDirectory() <= 0 && browserErrorMsg[0] != '\0')
			{
				WindowPrompt("Error", browserErrorMsg, "OK", nullptr);
				browserErrorMsg[0] = '\0';
			}
			fileBrowser.resetState();
			if(browser.numEntries > 0)
				fileBrowser.fileList[0]->setState(STATE::SELECTED);
			fileBrowser.triggerUpdate();
		}

		char currentPath[MAXPATHLEN + 130];
		if(rootdir[0] == '\0')
			snprintf(currentPath, sizeof(currentPath), "Select a device");
		else
			snprintf(currentPath, sizeof(currentPath), "%s%s", rootdir, browser.dir[0] == '/' && rootdir[strlen(rootdir) - 1] == '/' ? browser.dir + 1 : browser.dir);

		if(strcmp(currentPath, lastPath) != 0)
		{
			pathTxt.setText(currentPath);
			strcpy(lastPath, currentPath);
		}

		uint32_t pressed = 0;
		for(int i = 0; i < 4; i++)
			pressed |= controller[i]->getPadData().buttons_d;

		if((pressed & INPUT_BTN_B) && rootdir[0] != '\0' && browser.numEntries > 0 && strcmp(browserList[0].filename, "..") == 0)
		{
			browser.selIndex = 0;
			enterFolder();
			continue;
		}

		for(int i = 0; i < FILE_PAGESIZE; i++)
		{
			if(fileBrowser.fileList[i]->getState() != STATE::CLICKED)
				continue;

			fileBrowser.fileList[i]->resetState();

			if(browserList[browser.selIndex].isdir)
			{
				enterFolder();
				break;
			}

			char path[MAXPATHLEN];
			char error[128] = "";
			GetSelectedPath(path);

			HaltDeviceCheckingThread();
			mainWindow->setState(STATE::DISABLED);
			PlayResult result = PlayFile(path, error, sizeof(error));
			mainWindow->setState(STATE::DEFAULT);
			ResumeDeviceCheckingThread();

			if(result == PLAY_EXIT)
				return MENU_EXIT;
			if(result == PLAY_ERROR)
				WindowPrompt("Error", error, "OK", nullptr);
			break;
		}

		if(backBtn.getState() == STATE::CLICKED)
		{
			backBtn.resetState();
			break;
		}

		if(exitBtn.getState() == STATE::CLICKED)
		{
			platform->triggerExit();
			menu = MENU_EXIT;
		}
	}

	return menu;
}

struct ThumbTask
{
	Thread thread;
	Mutex lock;
	YtResult results[YT_MAX_RESULTS];
	int count = 0;
	void * textures[YT_MAX_RESULTS] = { nullptr };
	int widths[YT_MAX_RESULTS] = { 0 };
	int heights[YT_MAX_RESULTS] = { 0 };
	volatile bool ready[YT_MAX_RESULTS] = { false };
	volatile bool stop = false;
};

static ThumbTask * gThumbTask = nullptr;

static void * thumbThreadEntry(void * arg)
{
	ThumbTask * t = static_cast<ThumbTask *>(arg);
	for(int i = 0; i < t->count && !t->stop; i++)
	{
		int w = 0, h = 0;
		void * tex = nullptr;
		if(t->results[i].isChannel)
			tex = ytFetchImage(t->results[i].avatarUrl, 88, 88, &w, &h);
		else if(t->results[i].isPlaylist && t->results[i].avatarUrl[0] != '\0')
			tex = ytFetchImage(t->results[i].avatarUrl, 144, 81, &w, &h);
		else if(t->results[i].videoId[0] != '\0')
			tex = ytFetchThumbnail(t->results[i].videoId, 144, 81, &w, &h);
		if(t->stop)
		{
			if(tex) platform->getVideo()->getImageRenderer()->destroyTexture(tex);
			break;
		}
		if(tex)
		{
			t->lock.lock();
			t->textures[i] = tex;
			t->widths[i] = w;
			t->heights[i] = h;
			t->ready[i] = true;
			t->lock.unlock();
		}
	}
	return nullptr;
}

static void startAsyncThumbnails(const YtResult * results, int count)
{
	if(gThumbTask)
	{
		gThumbTask->stop = true;
		if(gThumbTask->thread.isRunning())
			gThumbTask->thread.join();
		for(int i = 0; i < YT_MAX_RESULTS; i++)
		{
			if(gThumbTask->textures[i])
			{
				platform->getVideo()->getImageRenderer()->destroyTexture(gThumbTask->textures[i]);
				gThumbTask->textures[i] = nullptr;
			}
		}
		delete gThumbTask;
		gThumbTask = nullptr;
	}

	if(count <= 0)
		return;

	gThumbTask = new ThumbTask();
	gThumbTask->count = count > YT_MAX_RESULTS ? YT_MAX_RESULTS : count;
	for(int i = 0; i < gThumbTask->count; i++)
		gThumbTask->results[i] = results[i];

	gThumbTask->thread.start(thumbThreadEntry, gThumbTask, 64 * 1024, ThreadPriority::Normal);
}

void MenuPlaylist(const char * playlistId, const char * playlistTitle)
{
	if(!playlistId || playlistId[0] == '\0')
		return;

	const PixelColor white = {255, 255, 255, 255};
	const PixelColor grey = {170, 175, 190, 255};
	const PixelColor subText = {135, 145, 170, 255};
	const PixelColor cardNormal = {26, 30, 42, 255};
	const PixelColor cardOver = {44, 52, 74, 255};
	const PixelColor tabActive = {0, 120, 215, 255};
	const PixelColor btnText = {25, 28, 38, 255};

	int sw = platform->getVideo()->getScreenWidth();
	int sh = platform->getVideo()->getScreenHeight();

	GuiImageData btnOutline(button_png);
	GuiImageData btnOutlineOver(button_over_png);
	GuiSound btnSoundOver(button_over_pcm, button_over_pcm_size, SOUND::PCM);
	GuiTrigger trigA;
	trigA.setPrimaryTrigger();

	char currentTitle[160] = "";
	char currentAuthor[96] = "";
	if(playlistTitle && playlistTitle[0])
		snprintf(currentTitle, sizeof(currentTitle), "%s", playlistTitle);

	const int MAX_PL_ITEMS = 60;
	std::unique_ptr<YtPlaylistItem[]> itemsPtr(new YtPlaylistItem[MAX_PL_ITEMS]);
	YtPlaylistItem * items = itemsPtr.get();
	int itemCount = 0;
	int page = 0;
	const int CARDS_PER_PAGE = 3;

	bool isLocal = (strncmp(playlistId, "local:", 6) == 0);
	if(isLocal)
	{
		YtLocalPlaylist localPl;
		if(!ytGetLocalPlaylist(playlistId + 6, localPl))
		{
			WindowPrompt("Playlist Error", "Local playlist not found", "OK", nullptr);
			return;
		}
		itemCount = (int)localPl.items.size();
		if(itemCount > MAX_PL_ITEMS) itemCount = MAX_PL_ITEMS;
		for(int i = 0; i < itemCount; i++)
		{
			memset(&items[i], 0, sizeof(items[i]));
			snprintf(items[i].videoId, sizeof(items[i].videoId), "%s", localPl.items[i].videoId);
			snprintf(items[i].title, sizeof(items[i].title), "%s", localPl.items[i].title);
			snprintf(items[i].author, sizeof(items[i].author), "%s", localPl.items[i].author);
			snprintf(items[i].duration, sizeof(items[i].duration), "%s", localPl.items[i].duration);
			snprintf(items[i].thumbUrl, sizeof(items[i].thumbUrl), "%s", localPl.items[i].thumbUrl);
		}
		if(localPl.title[0]) snprintf(currentTitle, sizeof(currentTitle), "%s", localPl.title);
		snprintf(currentAuthor, sizeof(currentAuthor), "Local Playlist");
		if(itemCount == 0)
		{
			WindowPrompt("Playlist", "This playlist is empty. Add videos from the player menu.", "OK", nullptr);
			return;
		}
	}
	else
	{
		struct PlBrowseTask
		{
			char id[64];
			char title[160];
			char author[96];
			YtPlaylistItem * itms;
			int maxItms;
			int count = 0;
			char error[128];
			bool success = false;
			volatile bool done = false;
		} bTask;

		snprintf(bTask.id, sizeof(bTask.id), "%s", playlistId);
		bTask.itms = items;
		bTask.maxItms = MAX_PL_ITEMS;
		bTask.error[0] = '\0';
		bTask.done = false;

		Thread bThread;
		bThread.start([](void * arg) -> void * {
			PlBrowseTask * t = static_cast<PlBrowseTask *>(arg);
			t->success = ytPlaylistBrowse(t->id, t->title, sizeof(t->title), t->author, sizeof(t->author), t->itms, t->maxItms, &t->count, t->error, sizeof(t->error));
			t->done = true;
			return nullptr;
		}, &bTask, 64 * 1024, ThreadPriority::Normal);

		RunWithLoadingScreen("BrewTube", "Loading playlist...", bTask.done);
		bThread.join();

		if(!bTask.success || bTask.count == 0)
		{
			WindowPrompt("Playlist Error", bTask.error[0] ? bTask.error : "Could not load playlist", "OK", nullptr);
			return;
		}

		itemCount = bTask.count;
		if(bTask.title[0]) snprintf(currentTitle, sizeof(currentTitle), "%s", bTask.title);
		if(bTask.author[0]) snprintf(currentAuthor, sizeof(currentAuthor), "%s", bTask.author);
	}

	struct PlThumbTask
	{
		Thread thread;
		Mutex lock;
		YtPlaylistItem items[MAX_PL_ITEMS];
		void * textures[MAX_PL_ITEMS] = { nullptr };
		int widths[MAX_PL_ITEMS] = { 0 };
		int heights[MAX_PL_ITEMS] = { 0 };
		bool ready[MAX_PL_ITEMS] = { false };
		int count = 0;
		volatile bool stop = false;
	};

	PlThumbTask * thumbTask = new PlThumbTask();
	thumbTask->count = itemCount;
	for(int i = 0; i < itemCount; i++)
		thumbTask->items[i] = items[i];

	thumbTask->thread.start([](void * arg) -> void * {
		PlThumbTask * t = static_cast<PlThumbTask *>(arg);
		for(int i = 0; i < t->count && !t->stop; i++)
		{
			int w = 0, h = 0;
			void * tex = nullptr;
			if(t->items[i].videoId[0] != '\0')
				tex = ytFetchThumbnail(t->items[i].videoId, 144, 81, &w, &h);
			else if(t->items[i].thumbUrl[0] != '\0')
				tex = ytFetchImage(t->items[i].thumbUrl, 144, 81, &w, &h);

			if(t->stop)
			{
				if(tex) platform->getVideo()->getImageRenderer()->destroyTexture(tex);
				break;
			}
			if(tex)
			{
				t->lock.lock();
				t->textures[i] = tex;
				t->widths[i] = w;
				t->heights[i] = h;
				t->ready[i] = true;
				t->lock.unlock();
			}
		}
		return nullptr;
	}, thumbTask, 64 * 1024, ThreadPriority::Normal);

	GuiImageData * thumbData[MAX_PL_ITEMS] = { nullptr };
	GuiImage * thumbImg[MAX_PL_ITEMS] = { nullptr };

	bool inPlaylist = true;
	while(inPlaylist && !platform->shouldExit())
	{
		GuiWindow plWin(sw, sh);

		GuiImage headerBg(sw, 56, (PixelColor){10, 12, 18, 255});
		GuiImage headerLine(sw, 2, tabActive);
		headerLine.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
		headerLine.setPosition(0, 56);

		char dispTitle[64];
		snprintf(dispTitle, sizeof(dispTitle), "%s", currentTitle[0] ? currentTitle : "Playlist");
		GuiText titleTxt(dispTitle, 22, white);
		titleTxt.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
		titleTxt.setPosition(25, 8);
		titleTxt.setMaxWidth(460);

		char metaInfo[128];
		snprintf(metaInfo, sizeof(metaInfo), "%s%s%d videos", currentAuthor[0] ? currentAuthor : "", currentAuthor[0] ? "   •   " : "", itemCount);
		GuiText authorTxt(metaInfo, 14, grey);
		authorTxt.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
		authorTxt.setPosition(25, 34);
		authorTxt.setMaxWidth(460);

		GuiImage backImg(&btnOutline);
		backImg.setSize(75, 34);
		GuiImage backImgOver(&btnOutlineOver);
		backImgOver.setSize(75, 34);
		GuiText backTxt("Back", 18, btnText);
		GuiButton backBtn(75, 34);
		backBtn.setAlignment(ALIGN_H::RIGHT, ALIGN_V::TOP);
		backBtn.setPosition(-16, 11);
		backBtn.setImage(&backImg);
		backBtn.setImageOver(&backImgOver);
		backBtn.setLabel(&backTxt);
		backBtn.setTrigger(&trigA);
		backBtn.setSoundOver(&btnSoundOver);
		backBtn.setEffectGrow();

		plWin.append(&headerBg);
		plWin.append(&headerLine);
		plWin.append(&titleTxt);
		plWin.append(&authorTxt);
		plWin.append(&backBtn);

		int startIdx = page * CARDS_PER_PAGE;
		int visibleCount = 0;
		int cardYStart = 68;
		int cardH = 94;
		int cardGap = 8;
		const int CARD_W = 590;

		GuiButton * cardBtn[CARDS_PER_PAGE] = { nullptr };
		GuiImage * cardBg[CARDS_PER_PAGE] = { nullptr };
		GuiImage * cardBgOver[CARDS_PER_PAGE] = { nullptr };
		GuiText * cardTitle[CARDS_PER_PAGE] = { nullptr };
		GuiText * cardAuthor[CARDS_PER_PAGE] = { nullptr };
		GuiText * cardMeta[CARDS_PER_PAGE] = { nullptr };
		GuiButton * delItemBtn[CARDS_PER_PAGE] = { nullptr };
		GuiImage * delItemImg[CARDS_PER_PAGE] = { nullptr };
		GuiImage * delItemImgOver[CARDS_PER_PAGE] = { nullptr };
		GuiText * delItemTxt[CARDS_PER_PAGE] = { nullptr };
		char cardTitleStr[CARDS_PER_PAGE][64];
		char cardAuthorStr[CARDS_PER_PAGE][64];
		char cardMetaStr[CARDS_PER_PAGE][64];

		for(int s = 0; s < CARDS_PER_PAGE; s++)
		{
			int idx = startIdx + s;
			if(idx >= itemCount) break;
			visibleCount++;
			int cardY = cardYStart + s * (cardH + cardGap);

			cardBg[s] = new GuiImage(CARD_W, cardH, cardNormal);
			cardBgOver[s] = new GuiImage(CARD_W, cardH, cardOver);

			cardBtn[s] = new GuiButton(CARD_W, cardH);
			cardBtn[s]->setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
			cardBtn[s]->setPosition(0, cardY);
			cardBtn[s]->setImage(cardBg[s]);
			cardBtn[s]->setImageOver(cardBgOver[s]);
			cardBtn[s]->setSoundOver(&btnSoundOver);
			cardBtn[s]->setTrigger(&trigA);
			cardBtn[s]->setEffectGrow();

			if(thumbImg[idx])
			{
				thumbImg[idx]->setAlignment(ALIGN_H::LEFT, ALIGN_V::MIDDLE);
				thumbImg[idx]->setPosition(8, 0);
				cardBtn[s]->setIcon(thumbImg[idx]);
			}

			snprintf(cardTitleStr[s], sizeof(cardTitleStr[s]), "%s", items[idx].title);
			if(strlen(items[idx].title) > 42)
			{
				cardTitleStr[s][39] = '.';
				cardTitleStr[s][40] = '.';
				cardTitleStr[s][41] = '.';
				cardTitleStr[s][42] = '\0';
			}
			cardTitle[s] = new GuiText(cardTitleStr[s], 17, white);
			cardTitle[s]->setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
			cardTitle[s]->setPosition(148, 12);
			cardTitle[s]->setMaxWidth(isLocal ? 350 : 430);
			cardBtn[s]->setLabel(cardTitle[s], 0);

			snprintf(cardAuthorStr[s], sizeof(cardAuthorStr[s]), "%s", items[idx].author);
			cardAuthor[s] = new GuiText(cardAuthorStr[s], 14, grey);
			cardAuthor[s]->setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
			cardAuthor[s]->setPosition(148, 38);
			cardAuthor[s]->setMaxWidth(isLocal ? 350 : 430);
			cardBtn[s]->setLabel(cardAuthor[s], 1);

			snprintf(cardMetaStr[s], sizeof(cardMetaStr[s]), "%s", items[idx].duration);
			cardMeta[s] = new GuiText(cardMetaStr[s], 13, subText);
			cardMeta[s]->setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
			cardMeta[s]->setPosition(148, 62);
			cardMeta[s]->setMaxWidth(isLocal ? 350 : 430);
			cardBtn[s]->setLabel(cardMeta[s], 2);

			plWin.append(cardBtn[s]);

			if(isLocal)
			{
				delItemImg[s] = new GuiImage(&btnOutline);
				delItemImg[s]->setSize(65, 30);
				delItemImgOver[s] = new GuiImage(&btnOutlineOver);
				delItemImgOver[s]->setSize(65, 30);
				delItemTxt[s] = new GuiText("Del", 15, btnText);

				delItemBtn[s] = new GuiButton(65, 30);
				delItemBtn[s]->setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
				delItemBtn[s]->setPosition((sw - CARD_W) / 2 + CARD_W - 75, cardY + (cardH - 30) / 2);
				delItemBtn[s]->setImage(delItemImg[s]);
				delItemBtn[s]->setImageOver(delItemImgOver[s]);
				delItemBtn[s]->setLabel(delItemTxt[s]);
				delItemBtn[s]->setSoundOver(&btnSoundOver);
				delItemBtn[s]->setTrigger(&trigA);
				delItemBtn[s]->setEffectGrow();

				plWin.append(delItemBtn[s]);
			}
		}

		int totalPages = (itemCount + CARDS_PER_PAGE - 1) / CARDS_PER_PAGE;
		GuiImage prevImg(&btnOutline);
		prevImg.setSize(90, 34);
		GuiImage prevImgOver(&btnOutlineOver);
		prevImgOver.setSize(90, 34);
		GuiText prevTxt("Prev", 18, btnText);
		GuiButton prevBtn(90, 34);
		prevBtn.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
		prevBtn.setPosition(-70, 428);
		prevBtn.setImage(&prevImg);
		prevBtn.setImageOver(&prevImgOver);
		prevBtn.setLabel(&prevTxt);
		prevBtn.setTrigger(&trigA);
		prevBtn.setSoundOver(&btnSoundOver);
		prevBtn.setEffectGrow();

		GuiImage nextImg(&btnOutline);
		nextImg.setSize(90, 34);
		GuiImage nextImgOver(&btnOutlineOver);
		nextImgOver.setSize(90, 34);
		GuiText nextTxt("Next", 18, btnText);
		GuiButton nextBtn(90, 34);
		nextBtn.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
		nextBtn.setPosition(70, 428);
		nextBtn.setImage(&nextImg);
		nextBtn.setImageOver(&nextImgOver);
		nextBtn.setLabel(&nextTxt);
		nextBtn.setTrigger(&trigA);
		nextBtn.setSoundOver(&btnSoundOver);
		nextBtn.setEffectGrow();

		char pageStr[32];
		snprintf(pageStr, sizeof(pageStr), "%d / %d", page + 1, totalPages > 0 ? totalPages : 1);
		GuiText pageTxt(pageStr, 17, white);
		pageTxt.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
		pageTxt.setPosition(0, 434);

		if(totalPages > 1)
		{
			if(page > 0) plWin.append(&prevBtn);
			plWin.append(&pageTxt);
			if(page + 1 < totalPages) plWin.append(&nextBtn);
		}

		mainWindow->appendWithAutoRemove(&plWin);
		bool stayInPage = true;

		while(stayInPage && inPlaylist && !platform->shouldExit())
		{
			if(!UpdateGui())
			{
				inPlaylist = false;
				break;
			}

			if(thumbTask)
			{
				thumbTask->lock.lock();
				for(int i = 0; i < itemCount; i++)
				{
					if(thumbTask->ready[i] && !thumbImg[i] && thumbTask->textures[i])
					{
						thumbData[i] = new GuiImageData(thumbTask->textures[i], thumbTask->widths[i], thumbTask->heights[i]);
						thumbTask->textures[i] = nullptr;
						thumbImg[i] = new GuiImage(thumbData[i]);
						thumbImg[i]->setSize(124, 70);
						thumbImg[i]->setAlignment(ALIGN_H::LEFT, ALIGN_V::MIDDLE);
						thumbImg[i]->setPosition(8, 0);

						for(int s = 0; s < visibleCount; s++)
						{
							if(startIdx + s == i && cardBtn[s])
								cardBtn[s]->setIcon(thumbImg[i]);
						}
					}
				}
				thumbTask->lock.unlock();
			}

			if(backBtn.getState() == STATE::CLICKED)
			{
				backBtn.resetState();
				inPlaylist = false;
				stayInPage = false;
				break;
			}

			for(int s = 0; s < visibleCount; s++)
			{
				int idx = startIdx + s;
				if(isLocal && delItemBtn[s] && delItemBtn[s]->getState() == STATE::CLICKED)
				{
					delItemBtn[s]->resetState();
					ytRemoveFromLocalPlaylist(playlistId + 6, items[idx].videoId);
					for(int k = idx; k < itemCount - 1; k++)
						items[k] = items[k + 1];
					itemCount--;
					stayInPage = false;
					break;
				}

				if(cardBtn[s] && cardBtn[s]->getState() == STATE::CLICKED)
				{
					cardBtn[s]->resetState();
					int chosen = startIdx + s;

					struct ResolveTask
					{
						char videoId[32];
						char streamUrl[8192];
						char error[128];
						YtResult * result;
						bool success = false;
						volatile bool done = false;
					};
					std::unique_ptr<ResolveTask> rTask(new ResolveTask());
					snprintf(rTask->videoId, sizeof(rTask->videoId), "%s", items[chosen].videoId);
					rTask->streamUrl[0] = '\0';
					rTask->error[0] = '\0';
					rTask->done = false;

					YtResult ytMeta = {};
					snprintf(ytMeta.videoId, sizeof(ytMeta.videoId), "%s", items[chosen].videoId);
					snprintf(ytMeta.title, sizeof(ytMeta.title), "%s", items[chosen].title);
					snprintf(ytMeta.author, sizeof(ytMeta.author), "%s", items[chosen].author);
					snprintf(ytMeta.lengthText, sizeof(ytMeta.lengthText), "%s", items[chosen].duration);
					rTask->result = &ytMeta;

					Thread resolveThread;
					resolveThread.start([](void * arg) -> void * {
						ResolveTask * t = static_cast<ResolveTask *>(arg);
						t->success = ytResolveStream(t->videoId, t->streamUrl, sizeof(t->streamUrl), t->error, sizeof(t->error), t->result);
						t->done = true;
						return nullptr;
					}, rTask.get(), 64 * 1024, ThreadPriority::Normal);

					RunWithLoadingScreen("BrewTube", "Resolving video stream", rTask->done);
					resolveThread.join();

					if(rTask->success)
					{
						HaltDeviceCheckingThread();
						mainWindow->setState(STATE::DISABLED);
						PlayResult playRes = PlayFile(rTask->streamUrl, rTask->error, sizeof(rTask->error), ytMeta.title, &ytMeta);
						mainWindow->setState(STATE::DEFAULT);
						ResumeDeviceCheckingThread();

						while(playRes == PLAY_NEXT_VIDEO)
						{
							chosen++;
							if(chosen >= itemCount) break;

							snprintf(rTask->videoId, sizeof(rTask->videoId), "%s", items[chosen].videoId);
							rTask->streamUrl[0] = '\0';
							rTask->error[0] = '\0';
							memset(&ytMeta, 0, sizeof(ytMeta));
							snprintf(ytMeta.videoId, sizeof(ytMeta.videoId), "%s", items[chosen].videoId);
							snprintf(ytMeta.title, sizeof(ytMeta.title), "%s", items[chosen].title);
							snprintf(ytMeta.author, sizeof(ytMeta.author), "%s", items[chosen].author);
							snprintf(ytMeta.lengthText, sizeof(ytMeta.lengthText), "%s", items[chosen].duration);
							rTask->result = &ytMeta;
							rTask->done = false;

							Thread nextThread;
							nextThread.start([](void * arg) -> void * {
								ResolveTask * t = static_cast<ResolveTask *>(arg);
								t->success = ytResolveStream(t->videoId, t->streamUrl, sizeof(t->streamUrl), t->error, sizeof(t->error), t->result);
								t->done = true;
								return nullptr;
							}, rTask.get(), 64 * 1024, ThreadPriority::Normal);

							RunWithLoadingScreen("BrewTube", "Resolving next video", rTask->done);
							nextThread.join();

							if(!rTask->success) break;

							HaltDeviceCheckingThread();
							mainWindow->setState(STATE::DISABLED);
							playRes = PlayFile(rTask->streamUrl, rTask->error, sizeof(rTask->error), ytMeta.title, &ytMeta);
							mainWindow->setState(STATE::DEFAULT);
							ResumeDeviceCheckingThread();
						}

						while(true)
						{
							platform->getInput()->update();
							bool held = false;
							for(int c = 0; c < 4; c++)
							{
								if(controller[c]->getPadData().buttons_h & (INPUT_BTN_B | INPUT_BTN_1))
									held = true;
							}
							if(!held) break;
							usleep(10000);
						}
						platform->getInput()->update();

						for(int i = 0; i < visibleCount; i++)
						{
							if(cardBtn[i]) cardBtn[i]->resetState();
						}

						if(playRes == PLAY_EXIT)
						{
							platform->triggerExit();
							inPlaylist = false;
							stayInPage = false;
							break;
						}
						if(playRes == PLAY_ERROR) WindowPrompt("Error", rTask->error, "OK", nullptr);
					}
					else
					{
						WindowPrompt("Playback Failed", rTask->error[0] ? rTask->error : "Could not resolve stream URL", "OK", nullptr);
					}
				}
			}
			if(!stayInPage) break;

			if(totalPages > 1 && page > 0 && prevBtn.getState() == STATE::CLICKED)
			{
				prevBtn.resetState();
				page--;
				stayInPage = false;
				break;
			}

			if(totalPages > 1 && page + 1 < totalPages && nextBtn.getState() == STATE::CLICKED)
			{
				nextBtn.resetState();
				page++;
				stayInPage = false;
				break;
			}

			uint32_t pressed = 0;
			for(int i = 0; i < 4; i++)
				pressed |= controller[i]->getPadData().buttons_d;

			if(pressed & (INPUT_BTN_B | INPUT_BTN_1))
			{
				inPlaylist = false;
				stayInPage = false;
				break;
			}
		}

		for(int s = 0; s < visibleCount; s++)
		{
			delete cardBtn[s];
			delete cardBg[s];
			delete cardBgOver[s];
			delete cardTitle[s];
			delete cardAuthor[s];
			delete cardMeta[s];
			if(delItemBtn[s]) delete delItemBtn[s];
			if(delItemImg[s]) delete delItemImg[s];
			if(delItemImgOver[s]) delete delItemImgOver[s];
			if(delItemTxt[s]) delete delItemTxt[s];
		}
	}

	if(thumbTask)
	{
		thumbTask->stop = true;
		if(thumbTask->thread.isRunning())
			thumbTask->thread.join();
		for(int i = 0; i < itemCount; i++)
		{
			if(thumbTask->textures[i])
				platform->getVideo()->getImageRenderer()->destroyTexture(thumbTask->textures[i]);
			delete thumbImg[i];
			delete thumbData[i];
		}
		delete thumbTask;
	}
}

void MenuChannel(const char * channelIdOrHandle, const char * channelTitle, const char * avatarUrl)
{
	if(!channelIdOrHandle || channelIdOrHandle[0] == '\0')
		return;

	const PixelColor white = {255, 255, 255, 255};
	const PixelColor grey = {170, 175, 190, 255};
	const PixelColor subText = {135, 145, 170, 255};
	const PixelColor cardNormal = {26, 30, 42, 255};
	const PixelColor cardOver = {44, 52, 74, 255};
	const PixelColor tabActive = {0, 120, 215, 255};
	const PixelColor btnText = {25, 28, 38, 255};

	int sw = platform->getVideo()->getScreenWidth();
	int sh = platform->getVideo()->getScreenHeight();

	GuiImageData btnOutline(button_png);
	GuiImageData btnOutlineOver(button_over_png);
	GuiSound btnSoundOver(button_over_pcm, button_over_pcm_size, SOUND::PCM);
	GuiTrigger trigA;
	trigA.setPrimaryTrigger();

	char currentChannel[128];
	snprintf(currentChannel, sizeof(currentChannel), "%s", channelIdOrHandle);

	YtChannelTab currentTab = YT_CHAN_TAB_VIDEOS;
	YtChannelFilter currentFilter = YT_CHAN_FILTER_NEWEST;
	std::unique_ptr<YtChannelDetails> detailsPtr(new YtChannelDetails());
	YtChannelDetails & details = *detailsPtr;
	memset(&details, 0, sizeof(details));
	if(channelTitle && channelTitle[0])
		snprintf(details.title, sizeof(details.title), "%s", channelTitle);
	if(avatarUrl && avatarUrl[0])
		snprintf(details.avatarUrl, sizeof(details.avatarUrl), "%s", avatarUrl);

	std::unique_ptr<YtChannelItem[]> itemsPtr(new YtChannelItem[30]);
	YtChannelItem * items = itemsPtr.get();
	int itemCount = 0;
	int page = 0;
	int aboutScroll = 0;
	const int CARDS_PER_PAGE = 3;

	struct HeaderTask
	{
		Thread thread;
		Mutex lock;
		char avatarUrl[256];
		char bannerUrl[256];
		void * avatarTex = nullptr;
		int avatarW = 0, avatarH = 0;
		void * bannerTex = nullptr;
		int bannerW = 0, bannerH = 0;
		volatile bool readyAvatar = false;
		volatile bool readyBanner = false;
		volatile bool stop = false;
	};

	HeaderTask * headerTask = nullptr;
	GuiImageData * avatarData = nullptr;
	GuiImage * avatarImg = nullptr;
	GuiImageData * bannerData = nullptr;
	GuiImage * bannerImg = nullptr;

	auto stopHeaderTask = [&]() {
		if(headerTask)
		{
			headerTask->stop = true;
			if(headerTask->thread.isRunning())
				headerTask->thread.join();
			if(headerTask->avatarTex)
				platform->getVideo()->getImageRenderer()->destroyTexture(headerTask->avatarTex);
			if(headerTask->bannerTex)
				platform->getVideo()->getImageRenderer()->destroyTexture(headerTask->bannerTex);
			delete headerTask;
			headerTask = nullptr;
		}
	};

	struct ChannelThumbTask
	{
		Thread thread;
		Mutex lock;
		YtChannelItem items[30];
		int count = 0;
		void * textures[30] = { nullptr };
		int widths[30] = { 0 };
		int heights[30] = { 0 };
		volatile bool ready[30] = { false };
		volatile bool stop = false;
	};

	ChannelThumbTask * thumbTask = nullptr;
	GuiImageData * thumbData[30] = { nullptr };
	GuiImage * thumbImg[30] = { nullptr };

	auto clearThumbnails = [&]() {
		if(thumbTask)
		{
			thumbTask->stop = true;
			if(thumbTask->thread.isRunning())
				thumbTask->thread.join();
			for(int i = 0; i < 30; i++)
			{
				if(thumbTask->textures[i])
				{
					platform->getVideo()->getImageRenderer()->destroyTexture(thumbTask->textures[i]);
					thumbTask->textures[i] = nullptr;
				}
			}
			delete thumbTask;
			thumbTask = nullptr;
		}
		for(int i = 0; i < 30; i++)
		{
			delete thumbImg[i];
			thumbImg[i] = nullptr;
			delete thumbData[i];
			thumbData[i] = nullptr;
		}
	};

	auto loadBrowse = [&]() -> bool {
		clearThumbnails();

		struct BrowseTask
		{
			char channel[128];
			YtChannelTab tab;
			YtChannelFilter filter;
			YtChannelDetails * d;
			YtChannelItem * itms;
			int maxItms;
			int count = 0;
			char error[128];
			bool success = false;
			volatile bool done = false;
		} bTask;

		snprintf(bTask.channel, sizeof(bTask.channel), "%s", currentChannel);
		bTask.tab = currentTab;
		bTask.filter = currentFilter;
		bTask.d = &details;
		bTask.itms = items;
		bTask.maxItms = 30;
		bTask.error[0] = '\0';
		bTask.done = false;

		Thread bThread;
		bThread.start([](void * arg) -> void * {
			BrowseTask * t = static_cast<BrowseTask *>(arg);
			t->success = ytChannelBrowse(t->channel, t->d, t->tab, t->filter, t->itms, t->maxItms, &t->count, t->error, sizeof(t->error));
			t->done = true;
			return nullptr;
		}, &bTask, 64 * 1024, ThreadPriority::Normal);

		RunWithLoadingScreen("BrewTube", "Loading channel...", bTask.done);
		bThread.join();

		if(!bTask.success)
		{
			WindowPrompt("Channel Error", bTask.error[0] ? bTask.error : "Could not load channel", "OK", nullptr);
			return false;
		}

		itemCount = bTask.count;
		page = 0;
		aboutScroll = 0;

		if(itemCount > 0)
		{
			thumbTask = new ChannelThumbTask();
			thumbTask->count = itemCount > 30 ? 30 : itemCount;
			for(int i = 0; i < thumbTask->count; i++)
				thumbTask->items[i] = items[i];

			thumbTask->thread.start([](void * arg) -> void * {
				ChannelThumbTask * t = static_cast<ChannelThumbTask *>(arg);
				for(int i = 0; i < t->count && !t->stop; i++)
				{
					int w = 0, h = 0;
					void * tex = nullptr;
					if(t->items[i].id[0] != '\0')
						tex = ytFetchThumbnail(t->items[i].id, 144, 81, &w, &h);
					else if(t->items[i].thumbUrl[0] != '\0')
						tex = ytFetchImage(t->items[i].thumbUrl, 144, 81, &w, &h);

					if(t->stop)
					{
						if(tex) platform->getVideo()->getImageRenderer()->destroyTexture(tex);
						break;
					}
					if(tex)
					{
						t->lock.lock();
						t->textures[i] = tex;
						t->widths[i] = w;
						t->heights[i] = h;
						t->ready[i] = true;
						t->lock.unlock();
					}
				}
				return nullptr;
			}, thumbTask, 64 * 1024, ThreadPriority::Normal);
		}

		return true;
	};

	auto startHeaderLoad = [&]() {
		stopHeaderTask();
		if(details.avatarUrl[0] != '\0' || details.bannerUrl[0] != '\0')
		{
			headerTask = new HeaderTask();
			snprintf(headerTask->avatarUrl, sizeof(headerTask->avatarUrl), "%s", details.avatarUrl);
			snprintf(headerTask->bannerUrl, sizeof(headerTask->bannerUrl), "%s", details.bannerUrl);

			headerTask->thread.start([](void * arg) -> void * {
				HeaderTask * t = static_cast<HeaderTask *>(arg);
				if(t->avatarUrl[0] != '\0' && !t->stop)
				{
					int w = 0, h = 0;
					void * tex = ytFetchImage(t->avatarUrl, 56, 56, &w, &h);
					if(!t->stop && tex)
					{
						t->lock.lock();
						t->avatarTex = tex;
						t->avatarW = w;
						t->avatarH = h;
						t->readyAvatar = true;
						t->lock.unlock();
					}
					else if(tex)
						platform->getVideo()->getImageRenderer()->destroyTexture(tex);
				}
				if(t->bannerUrl[0] != '\0' && !t->stop)
				{
					int w = 0, h = 0;
					void * tex = ytFetchImage(t->bannerUrl, 640, 74, &w, &h);
					if(!t->stop && tex)
					{
						t->lock.lock();
						t->bannerTex = tex;
						t->bannerW = w;
						t->bannerH = h;
						t->readyBanner = true;
						t->lock.unlock();
					}
					else if(tex)
						platform->getVideo()->getImageRenderer()->destroyTexture(tex);
				}
				return nullptr;
			}, headerTask, 64 * 1024, ThreadPriority::Normal);
		}
	};

	if(!loadBrowse())
		return;
	startHeaderLoad();

	bool inChannel = true;
	while(inChannel && !platform->shouldExit())
	{
		GuiWindow channelWin(sw, sh);

		GuiImage headerBg(sw, 74, (PixelColor){14, 16, 24, 255});
		channelWin.append(&headerBg);

		if(bannerImg)
			channelWin.append(bannerImg);

		GuiImage bannerTint(sw, 74, (PixelColor){0, 0, 0, 130});
		if(bannerImg)
			channelWin.append(&bannerTint);

		GuiImage avatarBox(48, 48, tabActive);
		avatarBox.setPosition(18, 13);
		if(avatarImg)
			channelWin.append(avatarImg);
		else
			channelWin.append(&avatarBox);

		GuiText titleTxt(details.title[0] ? details.title : (channelTitle ? channelTitle : currentChannel), 20, white);
		titleTxt.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
		titleTxt.setPosition(76, 14);
		titleTxt.setMaxWidth(sw - 170);
		channelWin.append(&titleTxt);

		char subStats[160];
		if(details.handle[0] != '\0' && details.subscriberCount[0] != '\0')
			snprintf(subStats, sizeof(subStats), "%s   •   %s", details.handle, details.subscriberCount);
		else if(details.handle[0] != '\0')
			snprintf(subStats, sizeof(subStats), "%s", details.handle);
		else if(details.subscriberCount[0] != '\0')
			snprintf(subStats, sizeof(subStats), "%s", details.subscriberCount);
		else
			snprintf(subStats, sizeof(subStats), "%s", currentChannel);

		GuiText handleTxt(subStats, 14, grey);
		handleTxt.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
		handleTxt.setPosition(76, 40);
		handleTxt.setMaxWidth(sw - 170);
		channelWin.append(&handleTxt);

		GuiImage backImg(&btnOutline);
		backImg.setSize(75, 34);
		GuiImage backImgOver(&btnOutlineOver);
		backImgOver.setSize(75, 34);
		GuiText backTxt("Back", 18, btnText);
		GuiButton backBtn(75, 34);
		backBtn.setAlignment(ALIGN_H::RIGHT, ALIGN_V::TOP);
		backBtn.setPosition(-16, 12);
		backBtn.setImage(&backImg);
		backBtn.setImageOver(&backImgOver);
		backBtn.setLabel(&backTxt);
		backBtn.setTrigger(&trigA);
		backBtn.setSoundOver(&btnSoundOver);
		backBtn.setEffectGrow();
		channelWin.append(&backBtn);

		bool isSub = ytIsSubscribed(details.channelId[0] ? details.channelId : currentChannel);
		GuiImage subImg(&btnOutline);
		subImg.setSize(110, 34);
		GuiImage subImgOver(&btnOutlineOver);
		subImgOver.setSize(110, 34);
		GuiText subTxt(isSub ? "Subscribed" : "Subscribe", 16, btnText);
		GuiButton subBtn(110, 34);
		subBtn.setAlignment(ALIGN_H::RIGHT, ALIGN_V::TOP);
		subBtn.setPosition(-98, 12);
		subBtn.setImage(&subImg);
		subBtn.setImageOver(&subImgOver);
		subBtn.setLabel(&subTxt);
		subBtn.setTrigger(&trigA);
		subBtn.setSoundOver(&btnSoundOver);
		subBtn.setEffectGrow();
		channelWin.append(&subBtn);

		GuiImage headerLine(sw, 2, tabActive);
		headerLine.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
		headerLine.setPosition(0, 74);
		channelWin.append(&headerLine);

		const char * tabTitles[5] = { "Videos", "Shorts", "Playlists", "Posts", "About" };
		std::vector<GuiImage> tabImg;
		std::vector<GuiImage> tabImgOver;
		std::vector<GuiText> tabTxt;
		tabImg.reserve(5);
		tabImgOver.reserve(5);
		tabTxt.reserve(5);
		GuiButton tabBtn[5];

		int tabStartX = (sw - (5 * 84 + 4 * 6)) / 2;
		for(int t = 0; t < 5; t++)
		{
			tabImg.emplace_back(84, 28, t == (int)currentTab ? tabActive : (PixelColor){32, 36, 48, 255});
			tabImgOver.emplace_back(84, 28, t == (int)currentTab ? tabActive : (PixelColor){50, 58, 80, 255});
			tabTxt.emplace_back(tabTitles[t], 16, white);

			tabBtn[t].setSize(84, 28);
			tabBtn[t].setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
			tabBtn[t].setPosition(tabStartX + t * (84 + 6), 80);
			tabBtn[t].setImage(&tabImg.back());
			tabBtn[t].setImageOver(&tabImgOver.back());
			tabBtn[t].setLabel(&tabTxt.back());
			tabBtn[t].setTrigger(&trigA);
			tabBtn[t].setSoundOver(&btnSoundOver);
			tabBtn[t].setEffectGrow();
			channelWin.append(&tabBtn[t]);
		}

		const char * filterTitles[3] = { "Latest", "Popular", "Oldest" };
		std::vector<GuiImage> filterImg;
		std::vector<GuiImage> filterImgOver;
		std::vector<GuiText> filterTxt;
		filterImg.reserve(3);
		filterImgOver.reserve(3);
		filterTxt.reserve(3);
		GuiButton filterBtn[3];

		if(currentTab == YT_CHAN_TAB_VIDEOS)
		{
			int filterStartX = (sw - (3 * 72 + 2 * 6)) / 2;
			for(int f = 0; f < 3; f++)
			{
				filterImg.emplace_back(72, 24, f == (int)currentFilter ? tabActive : (PixelColor){38, 44, 58, 255});
				filterImgOver.emplace_back(72, 24, f == (int)currentFilter ? tabActive : (PixelColor){55, 65, 85, 255});
				filterTxt.emplace_back(filterTitles[f], 14, white);

				filterBtn[f].setSize(72, 24);
				filterBtn[f].setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
				filterBtn[f].setPosition(filterStartX + f * (72 + 6), 114);
				filterBtn[f].setImage(&filterImg.back());
				filterBtn[f].setImageOver(&filterImgOver.back());
				filterBtn[f].setLabel(&filterTxt.back());
				filterBtn[f].setTrigger(&trigA);
				filterBtn[f].setSoundOver(&btnSoundOver);
				filterBtn[f].setEffectGrow();
				channelWin.append(&filterBtn[f]);
			}
		}

		int startIdx = page * CARDS_PER_PAGE;
		int visibleCount = 0;
		int cardYStart = (currentTab == YT_CHAN_TAB_VIDEOS) ? 144 : 116;
		int cardH = (currentTab == YT_CHAN_TAB_VIDEOS) ? 86 : 94;
		int cardGap = 8;
		const int CARD_W = 590;

		GuiButton * cardBtn[CARDS_PER_PAGE] = { nullptr };
		GuiImage * cardBg[CARDS_PER_PAGE] = { nullptr };
		GuiImage * cardBgOver[CARDS_PER_PAGE] = { nullptr };
		GuiText * cardTitle[CARDS_PER_PAGE] = { nullptr };
		GuiText * cardMeta[CARDS_PER_PAGE] = { nullptr };
		char cardTitleStr[CARDS_PER_PAGE][64];
		char cardMetaStr[CARDS_PER_PAGE][128];

		GuiImage aboutBg(590, 290, cardNormal);
		char aboutStr[1024];
		GuiText aboutTxt("", 15, white);
		char statsStr[256];
		GuiText statsTxt("", 13, subText);
		GuiText emptyTxt("No items found in this section", 18, grey);

		if(currentTab == YT_CHAN_TAB_ABOUT)
		{
			aboutBg.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
			aboutBg.setPosition(0, 118);
			channelWin.append(&aboutBg);

			snprintf(aboutStr, sizeof(aboutStr), "%s", details.description[0] ? details.description : "No description provided.");
			aboutTxt.setText(aboutStr);
			aboutTxt.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
			aboutTxt.setPosition((sw - 590) / 2 + 16, 134 - aboutScroll);
			aboutTxt.setWrap(true, 558);
			channelWin.append(&aboutTxt);

			snprintf(statsStr, sizeof(statsStr), "Channel ID: %s   •   Subscribers: %s   •   Videos: %s", details.channelId[0] ? details.channelId : currentChannel, details.subscriberCount[0] ? details.subscriberCount : "N/A", details.videoCount[0] ? details.videoCount : "N/A");
			statsTxt.setText(statsStr);
			statsTxt.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
			statsTxt.setPosition(0, 386);
			channelWin.append(&statsTxt);
		}
		else
		{
			if(itemCount == 0)
			{
				emptyTxt.setAlignment(ALIGN_H::CENTRE, ALIGN_V::MIDDLE);
				emptyTxt.setPosition(0, 30);
				channelWin.append(&emptyTxt);
			}
			else
			{
				for(int s = 0; s < CARDS_PER_PAGE; s++)
				{
					int idx = startIdx + s;
					if(idx >= itemCount) break;
					visibleCount++;
					int cardY = cardYStart + s * (cardH + cardGap);

					cardBg[s] = new GuiImage(CARD_W, cardH, cardNormal);
					cardBgOver[s] = new GuiImage(CARD_W, cardH, cardOver);

					cardBtn[s] = new GuiButton(CARD_W, cardH);
					cardBtn[s]->setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
					cardBtn[s]->setPosition(0, cardY);
					cardBtn[s]->setImage(cardBg[s]);
					cardBtn[s]->setImageOver(cardBgOver[s]);
					cardBtn[s]->setSoundOver(&btnSoundOver);
					cardBtn[s]->setTrigger(&trigA);
					if(items[idx].isPlayable || items[idx].id[0] != '\0')
						cardBtn[s]->setEffectGrow();

					if(thumbImg[idx])
					{
						thumbImg[idx]->setAlignment(ALIGN_H::LEFT, ALIGN_V::MIDDLE);
						thumbImg[idx]->setPosition(8, 0);
						cardBtn[s]->setIcon(thumbImg[idx]);
					}

					snprintf(cardTitleStr[s], sizeof(cardTitleStr[s]), "%s", items[idx].title);
					if(strlen(items[idx].title) > 42)
					{
						cardTitleStr[s][39] = '.';
						cardTitleStr[s][40] = '.';
						cardTitleStr[s][41] = '.';
						cardTitleStr[s][42] = '\0';
					}
					cardTitle[s] = new GuiText(cardTitleStr[s], 17, white);
					cardTitle[s]->setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
					cardTitle[s]->setPosition(148, 12);
					cardTitle[s]->setMaxWidth(430);
					cardBtn[s]->setLabel(cardTitle[s], 0);

					if(items[idx].date[0] != '\0' && items[idx].views[0] != '\0' && items[idx].duration[0] != '\0')
						snprintf(cardMetaStr[s], sizeof(cardMetaStr[s]), "%s   •   %s   •   %s", items[idx].duration, items[idx].views, items[idx].date);
					else if(items[idx].views[0] != '\0' && items[idx].duration[0] != '\0')
						snprintf(cardMetaStr[s], sizeof(cardMetaStr[s]), "%s   •   %s", items[idx].duration, items[idx].views);
					else if(items[idx].duration[0] != '\0')
						snprintf(cardMetaStr[s], sizeof(cardMetaStr[s]), "%s", items[idx].duration);
					else
						snprintf(cardMetaStr[s], sizeof(cardMetaStr[s]), "%s", items[idx].date);

					cardMeta[s] = new GuiText(cardMetaStr[s], 14, subText);
					cardMeta[s]->setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
					cardMeta[s]->setPosition(148, 38);
					cardMeta[s]->setMaxWidth(430);
					cardBtn[s]->setLabel(cardMeta[s], 1);

					channelWin.append(cardBtn[s]);
				}
			}
		}

		int totalPages = (itemCount + CARDS_PER_PAGE - 1) / CARDS_PER_PAGE;
		GuiImage prevImg(&btnOutline);
		prevImg.setSize(90, 34);
		GuiImage prevImgOver(&btnOutlineOver);
		prevImgOver.setSize(90, 34);
		GuiText prevTxt("Prev", 18, btnText);
		GuiButton prevBtn(90, 34);
		prevBtn.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
		prevBtn.setPosition(-70, 428);
		prevBtn.setImage(&prevImg);
		prevBtn.setImageOver(&prevImgOver);
		prevBtn.setLabel(&prevTxt);
		prevBtn.setTrigger(&trigA);
		prevBtn.setSoundOver(&btnSoundOver);
		prevBtn.setEffectGrow();

		GuiImage nextImg(&btnOutline);
		nextImg.setSize(90, 34);
		GuiImage nextImgOver(&btnOutlineOver);
		nextImgOver.setSize(90, 34);
		GuiText nextTxt("Next", 18, btnText);
		GuiButton nextBtn(90, 34);
		nextBtn.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
		nextBtn.setPosition(70, 428);
		nextBtn.setImage(&nextImg);
		nextBtn.setImageOver(&nextImgOver);
		nextBtn.setLabel(&nextTxt);
		nextBtn.setTrigger(&trigA);
		nextBtn.setSoundOver(&btnSoundOver);
		nextBtn.setEffectGrow();

		char pageStr[32];
		snprintf(pageStr, sizeof(pageStr), "%d / %d", page + 1, totalPages > 0 ? totalPages : 1);
		GuiText pageTxt(pageStr, 17, white);
		pageTxt.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
		pageTxt.setPosition(0, 434);

		if(currentTab != YT_CHAN_TAB_ABOUT && totalPages > 1)
		{
			if(page > 0) channelWin.append(&prevBtn);
			channelWin.append(&pageTxt);
			if(page + 1 < totalPages) channelWin.append(&nextBtn);
		}

		mainWindow->appendWithAutoRemove(&channelWin);
		bool stayInPage = true;

		while(stayInPage && inChannel && !platform->shouldExit())
		{
			if(!UpdateGui())
			{
				inChannel = false;
				break;
			}

			if(headerTask)
			{
				headerTask->lock.lock();
				if(headerTask->readyAvatar && headerTask->avatarTex && !avatarImg)
				{
					avatarData = new GuiImageData(headerTask->avatarTex, headerTask->avatarW, headerTask->avatarH);
					headerTask->avatarTex = nullptr;
					avatarImg = new GuiImage(avatarData);
					avatarImg->setSize(48, 48);
					avatarImg->setPosition(18, 13);
					stayInPage = false;
				}
				if(headerTask->readyBanner && headerTask->bannerTex && !bannerImg)
				{
					bannerData = new GuiImageData(headerTask->bannerTex, headerTask->bannerW, headerTask->bannerH);
					headerTask->bannerTex = nullptr;
					bannerImg = new GuiImage(bannerData);
					bannerImg->setSize(sw, 74);
					bannerImg->setPosition(0, 0);
					stayInPage = false;
				}
				headerTask->lock.unlock();
			}

			if(thumbTask)
			{
				thumbTask->lock.lock();
				for(int i = 0; i < itemCount; i++)
				{
					if(thumbTask->ready[i] && !thumbImg[i] && thumbTask->textures[i])
					{
						thumbData[i] = new GuiImageData(thumbTask->textures[i], thumbTask->widths[i], thumbTask->heights[i]);
						thumbTask->textures[i] = nullptr;
						thumbImg[i] = new GuiImage(thumbData[i]);
						thumbImg[i]->setSize(124, 70);
						thumbImg[i]->setAlignment(ALIGN_H::LEFT, ALIGN_V::MIDDLE);
						thumbImg[i]->setPosition(8, 0);

						for(int s = 0; s < visibleCount; s++)
						{
							if(startIdx + s == i && cardBtn[s])
							{
								cardBtn[s]->setIcon(thumbImg[i]);
							}
						}
					}
				}
				thumbTask->lock.unlock();
			}

			if(backBtn.getState() == STATE::CLICKED)
			{
				backBtn.resetState();
				inChannel = false;
				stayInPage = false;
				break;
			}

			if(subBtn.getState() == STATE::CLICKED)
			{
				subBtn.resetState();
				const char * cid = details.channelId[0] ? details.channelId : currentChannel;
				ytToggleSubscription(cid, details.title[0] ? details.title : currentChannel, details.avatarUrl);
				isSub = ytIsSubscribed(cid);
				subTxt.setText(isSub ? "Subscribed" : "Subscribe");
			}

			for(int t = 0; t < 5; t++)
			{
				if(tabBtn[t].getState() == STATE::CLICKED)
				{
					tabBtn[t].resetState();
					if(currentTab != (YtChannelTab)t)
					{
						currentTab = (YtChannelTab)t;
						loadBrowse();
						stayInPage = false;
						break;
					}
				}
			}
			if(!stayInPage) break;

			if(currentTab == YT_CHAN_TAB_VIDEOS)
			{
				for(int f = 0; f < 3; f++)
				{
					if(filterBtn[f].getState() == STATE::CLICKED)
					{
						filterBtn[f].resetState();
						if(currentFilter != (YtChannelFilter)f)
						{
							currentFilter = (YtChannelFilter)f;
							loadBrowse();
							stayInPage = false;
							break;
						}
					}
				}
				if(!stayInPage) break;
			}

			for(int s = 0; s < visibleCount; s++)
			{
				if(cardBtn[s] && cardBtn[s]->getState() == STATE::CLICKED)
				{
					cardBtn[s]->resetState();
					int chosen = startIdx + s;
					if(items[chosen].isPlayable)
					{
						if(thumbTask)
						{
							thumbTask->stop = true;
							if(thumbTask->thread.isRunning())
								thumbTask->thread.join();
						}
						if(headerTask)
						{
							headerTask->stop = true;
							if(headerTask->thread.isRunning())
								headerTask->thread.join();
						}

						struct ResolveTask
						{
							char videoId[32];
							char streamUrl[8192];
							char error[128];
							YtResult * result;
							bool success = false;
							volatile bool done = false;
						};
						std::unique_ptr<ResolveTask> rTask(new ResolveTask());

						snprintf(rTask->videoId, sizeof(rTask->videoId), "%s", items[chosen].id);
						rTask->streamUrl[0] = '\0';
						rTask->error[0] = '\0';
						rTask->done = false;

						YtResult ytMeta = {};
						snprintf(ytMeta.videoId, sizeof(ytMeta.videoId), "%s", items[chosen].id);
						snprintf(ytMeta.channelId, sizeof(ytMeta.channelId), "%s", details.channelId);
						snprintf(ytMeta.title, sizeof(ytMeta.title), "%s", items[chosen].title);
						snprintf(ytMeta.author, sizeof(ytMeta.author), "%s", details.title[0] ? details.title : currentChannel);
						snprintf(ytMeta.lengthText, sizeof(ytMeta.lengthText), "%s", items[chosen].duration);
						snprintf(ytMeta.viewCountText, sizeof(ytMeta.viewCountText), "%s", items[chosen].views);
						snprintf(ytMeta.publishedText, sizeof(ytMeta.publishedText), "%s", items[chosen].date);
						snprintf(ytMeta.avatarUrl, sizeof(ytMeta.avatarUrl), "%s", details.avatarUrl);
						rTask->result = &ytMeta;

						Thread resolveThread;
						resolveThread.start([](void * arg) -> void * {
							ResolveTask * t = static_cast<ResolveTask *>(arg);
							t->success = ytResolveStream(t->videoId, t->streamUrl, sizeof(t->streamUrl), t->error, sizeof(t->error), t->result);
							t->done = true;
							return nullptr;
						}, rTask.get(), 64 * 1024, ThreadPriority::Normal);

						RunWithLoadingScreen("BrewTube", "Resolving video stream", rTask->done);
						resolveThread.join();

						if(rTask->success)
						{
							HaltDeviceCheckingThread();
							mainWindow->setState(STATE::DISABLED);
							PlayResult playRes = PlayFile(rTask->streamUrl, rTask->error, sizeof(rTask->error), ytMeta.title, &ytMeta);
							mainWindow->setState(STATE::DEFAULT);
							ResumeDeviceCheckingThread();

							while(playRes == PLAY_NEXT_VIDEO)
							{
								YtResult nextVid;
								if(!GetNextVideo(&nextVid))
									break;

								snprintf(rTask->videoId, sizeof(rTask->videoId), "%s", nextVid.videoId);
								rTask->streamUrl[0] = '\0';
								rTask->error[0] = '\0';
								rTask->result = &nextVid;
								rTask->done = false;

								Thread nextThread;
								nextThread.start([](void * arg) -> void * {
									ResolveTask * t = static_cast<ResolveTask *>(arg);
									t->success = ytResolveStream(t->videoId, t->streamUrl, sizeof(t->streamUrl), t->error, sizeof(t->error), t->result);
									t->done = true;
									return nullptr;
								}, rTask.get(), 64 * 1024, ThreadPriority::Normal);

								RunWithLoadingScreen("BrewTube", "Resolving video stream", rTask->done);
								nextThread.join();

								if(rTask->success)
								{
									HaltDeviceCheckingThread();
									mainWindow->setState(STATE::DISABLED);
									playRes = PlayFile(rTask->streamUrl, rTask->error, sizeof(rTask->error), nextVid.title, &nextVid);
									mainWindow->setState(STATE::DEFAULT);
									ResumeDeviceCheckingThread();
								}
								else
								{
									WindowPrompt("Playback Failed", rTask->error[0] ? rTask->error : "Could not resolve stream URL", "OK", nullptr);
									break;
								}
							}

							while(true)
							{
								platform->getInput()->update();
								bool held = false;
								for(int c = 0; c < 4; c++)
								{
									if(controller[c]->getPadData().buttons_h & (INPUT_BTN_B | INPUT_BTN_1))
										held = true;
								}
								if(!held) break;
								usleep(10000);
							}
							platform->getInput()->update();

							for(int i = 0; i < visibleCount; i++)
							{
								if(cardBtn[i]) cardBtn[i]->resetState();
							}

							if(playRes == PLAY_EXIT)
							{
								platform->triggerExit();
								inChannel = false;
								stayInPage = false;
								break;
							}
							if(playRes == PLAY_CHANNEL)
							{
								char nextChan[64], nextAuthor[128], nextAvatar[256];
								if(GetNextChannel(nextChan, sizeof(nextChan), nextAuthor, sizeof(nextAuthor), nextAvatar, sizeof(nextAvatar)))
								{
									snprintf(currentChannel, sizeof(currentChannel), "%s", nextChan);
									memset(&details, 0, sizeof(details));
									if(nextAuthor[0]) snprintf(details.title, sizeof(details.title), "%s", nextAuthor);
									if(nextAvatar[0]) snprintf(details.avatarUrl, sizeof(details.avatarUrl), "%s", nextAvatar);
									currentTab = YT_CHAN_TAB_VIDEOS;
									currentFilter = YT_CHAN_FILTER_NEWEST;
									delete avatarImg; avatarImg = nullptr;
									delete avatarData; avatarData = nullptr;
									delete bannerImg; bannerImg = nullptr;
									delete bannerData; bannerData = nullptr;
									loadBrowse();
									startHeaderLoad();
									stayInPage = false;
									break;
								}
							}
							if(playRes == PLAY_ERROR) WindowPrompt("Error", rTask->error, "OK", nullptr);
						}
						else
						{
							WindowPrompt("Playback Failed", rTask->error[0] ? rTask->error : "Could not resolve stream URL", "OK", nullptr);
						}
					}
					else if(items[chosen].id[0] != '\0')
					{
						if(thumbTask)
						{
							thumbTask->stop = true;
							if(thumbTask->thread.isRunning())
								thumbTask->thread.join();
						}
						if(headerTask)
						{
							headerTask->stop = true;
							if(headerTask->thread.isRunning())
								headerTask->thread.join();
						}
						mainWindow->remove(&channelWin);
						MenuPlaylist(items[chosen].id, items[chosen].title);
						loadBrowse();
						startHeaderLoad();
						stayInPage = false;
						break;
					}
				}
			}
			if(!stayInPage) break;

			if(totalPages > 1 && page > 0 && prevBtn.getState() == STATE::CLICKED)
			{
				prevBtn.resetState();
				page--;
				stayInPage = false;
				break;
			}

			if(totalPages > 1 && page + 1 < totalPages && nextBtn.getState() == STATE::CLICKED)
			{
				nextBtn.resetState();
				page++;
				stayInPage = false;
				break;
			}

			uint32_t pressed = 0;
			for(int i = 0; i < 4; i++)
				pressed |= controller[i]->getPadData().buttons_d;

			if(pressed & (INPUT_BTN_B | INPUT_BTN_1))
			{
				inChannel = false;
				stayInPage = false;
				break;
			}

			if(totalPages > 1 && (pressed & INPUT_BTN_LEFT))
			{
				if(page > 0)
				{
					page--;
					stayInPage = false;
					break;
				}
			}

			if(totalPages > 1 && (pressed & INPUT_BTN_RIGHT))
			{
				if(page + 1 < totalPages)
				{
					page++;
					stayInPage = false;
					break;
				}
			}

			if(currentTab == YT_CHAN_TAB_ABOUT)
			{
				if(pressed & INPUT_BTN_UP)
				{
					if(aboutScroll > 0)
					{
						aboutScroll -= 30;
						if(aboutScroll < 0) aboutScroll = 0;
						stayInPage = false;
						break;
					}
				}
				if(pressed & INPUT_BTN_DOWN)
				{
					if(aboutScroll < 400)
					{
						aboutScroll += 30;
						stayInPage = false;
						break;
					}
				}
			}
		}

		if(currentTab != YT_CHAN_TAB_ABOUT)
		{
			for(int s = 0; s < visibleCount; s++)
			{
				delete cardBtn[s];
				delete cardBg[s];
				delete cardBgOver[s];
				delete cardTitle[s];
				delete cardMeta[s];
			}
		}
	}

	stopHeaderTask();
	clearThumbnails();
	delete avatarImg;
	delete avatarData;
	delete bannerImg;
	delete bannerData;
}

void MenuSubscriptions()
{
	const PixelColor white = {255, 255, 255, 255};
	const PixelColor grey = {170, 175, 190, 255};
	const PixelColor cardNormal = {26, 30, 42, 255};
	const PixelColor cardOver = {44, 52, 74, 255};
	const PixelColor tabActive = {0, 120, 215, 255};
	const PixelColor btnText = {25, 28, 38, 255};

	int sw = platform->getVideo()->getScreenWidth();
	int sh = platform->getVideo()->getScreenHeight();

	GuiImageData btnOutline(button_png);
	GuiImageData btnOutlineOver(button_over_png);
	GuiSound btnSoundOver(button_over_pcm, button_over_pcm_size, SOUND::PCM);
	GuiTrigger trigA;
	trigA.setPrimaryTrigger();

	std::vector<YtSubscription> subs = ytGetSubscriptions();
	int page = 0;
	const int CARDS_PER_PAGE = 4;

	bool inSubs = true;
	while(inSubs && !platform->shouldExit())
	{
		GuiWindow subsWin(sw, sh);

		GuiImage headerBg(sw, 52, (PixelColor){10, 12, 18, 255});
		GuiImage headerLine(sw, 2, tabActive);
		headerLine.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
		headerLine.setPosition(0, 52);

		GuiText titleTxt("Subscriptions", 24, white);
		titleTxt.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
		titleTxt.setPosition(25, 12);

		char countStr[32];
		snprintf(countStr, sizeof(countStr), "%zu channels", subs.size());
		GuiText countTxt(countStr, 16, grey);
		countTxt.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
		countTxt.setPosition(180, 18);

		GuiImage backImg(&btnOutline);
		backImg.setSize(75, 34);
		GuiImage backImgOver(&btnOutlineOver);
		backImgOver.setSize(75, 34);
		GuiText backTxt("Back", 18, btnText);
		GuiButton backBtn(75, 34);
		backBtn.setAlignment(ALIGN_H::RIGHT, ALIGN_V::TOP);
		backBtn.setPosition(-16, 9);
		backBtn.setImage(&backImg);
		backBtn.setImageOver(&backImgOver);
		backBtn.setLabel(&backTxt);
		backBtn.setTrigger(&trigA);
		backBtn.setSoundOver(&btnSoundOver);
		backBtn.setEffectGrow();

		subsWin.append(&headerBg);
		subsWin.append(&headerLine);
		subsWin.append(&titleTxt);
		subsWin.append(&countTxt);
		subsWin.append(&backBtn);

		int totalCount = (int)subs.size();
		int totalPages = (totalCount + CARDS_PER_PAGE - 1) / CARDS_PER_PAGE;
		if(page >= totalPages && totalPages > 0) page = totalPages - 1;

		GuiText emptyTxt("No subscribed channels yet.", 20, grey);
		emptyTxt.setAlignment(ALIGN_H::CENTRE, ALIGN_V::MIDDLE);
		emptyTxt.setPosition(0, 0);

		int startIdx = page * CARDS_PER_PAGE;
		int visibleCount = 0;
		int cardYStart = 68;
		int cardH = 74;
		int cardGap = 8;
		const int CARD_W = 590;

		GuiButton * cardBtn[CARDS_PER_PAGE] = { nullptr };
		GuiImage * cardBg[CARDS_PER_PAGE] = { nullptr };
		GuiImage * cardBgOver[CARDS_PER_PAGE] = { nullptr };
		GuiText * cardTitle[CARDS_PER_PAGE] = { nullptr };
		GuiText * cardId[CARDS_PER_PAGE] = { nullptr };
		GuiButton * unsubBtn[CARDS_PER_PAGE] = { nullptr };
		GuiImage * unsubImg[CARDS_PER_PAGE] = { nullptr };
		GuiImage * unsubImgOver[CARDS_PER_PAGE] = { nullptr };
		GuiText * unsubTxt[CARDS_PER_PAGE] = { nullptr };
		char cardTitleStr[CARDS_PER_PAGE][64];
		char cardIdStr[CARDS_PER_PAGE][64];

		if(totalCount == 0)
		{
			subsWin.append(&emptyTxt);
		}
		else
		{
			for(int s = 0; s < CARDS_PER_PAGE; s++)
			{
				int idx = startIdx + s;
				if(idx >= totalCount) break;
				visibleCount++;
				int cardY = cardYStart + s * (cardH + cardGap);

				cardBg[s] = new GuiImage(CARD_W, cardH, cardNormal);
				cardBgOver[s] = new GuiImage(CARD_W, cardH, cardOver);

				cardBtn[s] = new GuiButton(CARD_W, cardH);
				cardBtn[s]->setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
				cardBtn[s]->setPosition(0, cardY);
				cardBtn[s]->setImage(cardBg[s]);
				cardBtn[s]->setImageOver(cardBgOver[s]);
				cardBtn[s]->setSoundOver(&btnSoundOver);
				cardBtn[s]->setTrigger(&trigA);
				cardBtn[s]->setEffectGrow();

				snprintf(cardTitleStr[s], sizeof(cardTitleStr[s]), "%s", subs[idx].title[0] ? subs[idx].title : subs[idx].channelId);
				cardTitle[s] = new GuiText(cardTitleStr[s], 18, white);
				cardTitle[s]->setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
				cardTitle[s]->setPosition(20, 14);
				cardTitle[s]->setMaxWidth(420);
				cardBtn[s]->setLabel(cardTitle[s], 0);

				snprintf(cardIdStr[s], sizeof(cardIdStr[s]), "%s", subs[idx].channelId);
				cardId[s] = new GuiText(cardIdStr[s], 14, grey);
				cardId[s]->setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
				cardId[s]->setPosition(20, 42);
				cardId[s]->setMaxWidth(420);
				cardBtn[s]->setLabel(cardId[s], 1);

				unsubImg[s] = new GuiImage(&btnOutline);
				unsubImg[s]->setSize(100, 32);
				unsubImgOver[s] = new GuiImage(&btnOutlineOver);
				unsubImgOver[s]->setSize(100, 32);
				unsubTxt[s] = new GuiText("Remove", 16, btnText);
				unsubBtn[s] = new GuiButton(100, 32);
				unsubBtn[s]->setAlignment(ALIGN_H::RIGHT, ALIGN_V::TOP);
				unsubBtn[s]->setPosition(-16, cardY + 20);
				unsubBtn[s]->setImage(unsubImg[s]);
				unsubBtn[s]->setImageOver(unsubImgOver[s]);
				unsubBtn[s]->setLabel(unsubTxt[s]);
				unsubBtn[s]->setTrigger(&trigA);
				unsubBtn[s]->setSoundOver(&btnSoundOver);
				unsubBtn[s]->setEffectGrow();

				subsWin.append(cardBtn[s]);
				subsWin.append(unsubBtn[s]);
			}
		}

		GuiImage prevImg(&btnOutline);
		prevImg.setSize(90, 34);
		GuiImage prevImgOver(&btnOutlineOver);
		prevImgOver.setSize(90, 34);
		GuiText prevTxt("Prev", 18, btnText);
		GuiButton prevBtn(90, 34);
		prevBtn.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
		prevBtn.setPosition(-70, 428);
		prevBtn.setImage(&prevImg);
		prevBtn.setImageOver(&prevImgOver);
		prevBtn.setLabel(&prevTxt);
		prevBtn.setTrigger(&trigA);
		prevBtn.setSoundOver(&btnSoundOver);
		prevBtn.setEffectGrow();

		GuiImage nextImg(&btnOutline);
		nextImg.setSize(90, 34);
		GuiImage nextImgOver(&btnOutlineOver);
		nextImgOver.setSize(90, 34);
		GuiText nextTxt("Next", 18, btnText);
		GuiButton nextBtn(90, 34);
		nextBtn.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
		nextBtn.setPosition(70, 428);
		nextBtn.setImage(&nextImg);
		nextBtn.setImageOver(&nextImgOver);
		nextBtn.setLabel(&nextTxt);
		nextBtn.setTrigger(&trigA);
		nextBtn.setSoundOver(&btnSoundOver);
		nextBtn.setEffectGrow();

		char pageStr[32];
		snprintf(pageStr, sizeof(pageStr), "%d / %d", page + 1, totalPages > 0 ? totalPages : 1);
		GuiText pageTxt(pageStr, 17, white);
		pageTxt.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
		pageTxt.setPosition(0, 434);

		if(totalPages > 1)
		{
			if(page > 0) subsWin.append(&prevBtn);
			subsWin.append(&pageTxt);
			if(page + 1 < totalPages) subsWin.append(&nextBtn);
		}

		mainWindow->appendWithAutoRemove(&subsWin);
		bool stayInPage = true;

		while(stayInPage && inSubs && !platform->shouldExit())
		{
			if(!UpdateGui())
			{
				inSubs = false;
				break;
			}

			if(backBtn.getState() == STATE::CLICKED)
			{
				backBtn.resetState();
				inSubs = false;
				stayInPage = false;
				break;
			}

			for(int s = 0; s < visibleCount; s++)
			{
				if(unsubBtn[s] && unsubBtn[s]->getState() == STATE::CLICKED)
				{
					unsubBtn[s]->resetState();
					int idx = startIdx + s;
					ytToggleSubscription(subs[idx].channelId);
					subs = ytGetSubscriptions();
					stayInPage = false;
					break;
				}

				if(cardBtn[s] && cardBtn[s]->getState() == STATE::CLICKED)
				{
					cardBtn[s]->resetState();
					int idx = startIdx + s;
					mainWindow->remove(&subsWin);
					MenuChannel(subs[idx].channelId, subs[idx].title, subs[idx].avatarUrl);
					subs = ytGetSubscriptions();
					stayInPage = false;
					break;
				}
			}
			if(!stayInPage) break;

			if(totalPages > 1 && page > 0 && prevBtn.getState() == STATE::CLICKED)
			{
				prevBtn.resetState();
				page--;
				stayInPage = false;
				break;
			}

			if(totalPages > 1 && page + 1 < totalPages && nextBtn.getState() == STATE::CLICKED)
			{
				nextBtn.resetState();
				page++;
				stayInPage = false;
				break;
			}

			uint32_t pressed = 0;
			for(int i = 0; i < 4; i++)
				pressed |= controller[i]->getPadData().buttons_d;

			if(pressed & (INPUT_BTN_B | INPUT_BTN_1))
			{
				inSubs = false;
				stayInPage = false;
				break;
			}
		}

		for(int s = 0; s < visibleCount; s++)
		{
			delete cardBtn[s];
			delete cardBg[s];
			delete cardBgOver[s];
			delete cardTitle[s];
			delete cardId[s];
			delete unsubBtn[s];
			delete unsubImg[s];
			delete unsubImgOver[s];
			delete unsubTxt[s];
		}
	}
}

void MenuLocalPlaylists()
{
	const PixelColor white = {255, 255, 255, 255};
	const PixelColor grey = {170, 175, 190, 255};
	const PixelColor tabActive = {0, 120, 215, 255};
	const PixelColor btnText = {25, 28, 38, 255};
	const PixelColor cardNormal = {26, 30, 42, 255};
	const PixelColor cardOver = {44, 52, 74, 255};

	int sw = platform->getVideo()->getScreenWidth();
	int sh = platform->getVideo()->getScreenHeight();

	GuiImageData btnOutline(button_png);
	GuiImageData btnOutlineOver(button_over_png);
	GuiSound btnSoundOver(button_over_pcm, button_over_pcm_size, SOUND::PCM);
	GuiTrigger trigA;
	trigA.setPrimaryTrigger();

	int page = 0;
	const int CARDS_PER_PAGE = 4;

	bool inMenu = true;
	while(inMenu && !platform->shouldExit())
	{
		std::vector<YtLocalPlaylist> playlists = ytGetLocalPlaylists();

		GuiWindow plWin(sw, sh);

		GuiImage headerBg(sw, 52, (PixelColor){10, 12, 18, 255});
		GuiImage headerLine(sw, 2, tabActive);
		headerLine.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
		headerLine.setPosition(0, 52);

		GuiText titleTxt("Local Playlists", 24, white);
		titleTxt.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
		titleTxt.setPosition(25, 12);

		char countStr[32];
		snprintf(countStr, sizeof(countStr), "%zu playlists", playlists.size());
		GuiText countTxt(countStr, 16, grey);
		countTxt.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
		countTxt.setPosition(185, 18);

		GuiImage newImg(&btnOutline);
		newImg.setSize(95, 34);
		GuiImage newImgOver(&btnOutlineOver);
		newImgOver.setSize(95, 34);
		GuiText newTxt("+ New", 18, btnText);
		GuiButton newBtn(95, 34);
		newBtn.setAlignment(ALIGN_H::RIGHT, ALIGN_V::TOP);
		newBtn.setPosition(-100, 9);
		newBtn.setImage(&newImg);
		newBtn.setImageOver(&newImgOver);
		newBtn.setLabel(&newTxt);
		newBtn.setTrigger(&trigA);
		newBtn.setSoundOver(&btnSoundOver);
		newBtn.setEffectGrow();

		GuiImage backImg(&btnOutline);
		backImg.setSize(75, 34);
		GuiImage backImgOver(&btnOutlineOver);
		backImgOver.setSize(75, 34);
		GuiText backTxt("Back", 18, btnText);
		GuiButton backBtn(75, 34);
		backBtn.setAlignment(ALIGN_H::RIGHT, ALIGN_V::TOP);
		backBtn.setPosition(-16, 9);
		backBtn.setImage(&backImg);
		backBtn.setImageOver(&backImgOver);
		backBtn.setLabel(&backTxt);
		backBtn.setTrigger(&trigA);
		backBtn.setSoundOver(&btnSoundOver);
		backBtn.setEffectGrow();

		plWin.append(&headerBg);
		plWin.append(&headerLine);
		plWin.append(&titleTxt);
		plWin.append(&countTxt);
		plWin.append(&newBtn);
		plWin.append(&backBtn);

		int totalCount = (int)playlists.size();
		int totalPages = (totalCount + CARDS_PER_PAGE - 1) / CARDS_PER_PAGE;
		if(page >= totalPages && totalPages > 0) page = totalPages - 1;

		GuiText emptyTxt("No local playlists yet. Click '+ New' or add videos from the player menu.", 17, grey);
		emptyTxt.setAlignment(ALIGN_H::CENTRE, ALIGN_V::MIDDLE);
		emptyTxt.setPosition(0, 0);

		int startIdx = page * CARDS_PER_PAGE;
		int visibleCount = 0;
		int cardYStart = 68;
		int cardH = 74;
		int cardGap = 8;
		const int CARD_W = 590;

		GuiButton * cardBtn[CARDS_PER_PAGE] = { nullptr };
		GuiImage * cardBg[CARDS_PER_PAGE] = { nullptr };
		GuiImage * cardBgOver[CARDS_PER_PAGE] = { nullptr };
		GuiText * cardTitle[CARDS_PER_PAGE] = { nullptr };
		GuiText * cardInfo[CARDS_PER_PAGE] = { nullptr };
		GuiButton * delBtn[CARDS_PER_PAGE] = { nullptr };
		GuiImage * delImg[CARDS_PER_PAGE] = { nullptr };
		GuiImage * delImgOver[CARDS_PER_PAGE] = { nullptr };
		GuiText * delTxt[CARDS_PER_PAGE] = { nullptr };
		char cardTitleStr[CARDS_PER_PAGE][64];
		char cardInfoStr[CARDS_PER_PAGE][64];

		if(totalCount == 0)
		{
			plWin.append(&emptyTxt);
		}
		else
		{
			for(int s = 0; s < CARDS_PER_PAGE; s++)
			{
				int idx = startIdx + s;
				if(idx >= totalCount) break;
				visibleCount++;

				int cy = cardYStart + s * (cardH + cardGap);

				cardBg[s] = new GuiImage(CARD_W, cardH, cardNormal);
				cardBgOver[s] = new GuiImage(CARD_W, cardH, cardOver);

				cardBtn[s] = new GuiButton(CARD_W - 90, cardH);
				cardBtn[s]->setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
				cardBtn[s]->setPosition((sw - CARD_W) / 2, cy);
				cardBtn[s]->setImage(cardBg[s]);
				cardBtn[s]->setImageOver(cardBgOver[s]);
				cardBtn[s]->setSoundOver(&btnSoundOver);
				cardBtn[s]->setTrigger(&trigA);
				cardBtn[s]->setEffectGrow();

				snprintf(cardTitleStr[s], sizeof(cardTitleStr[s]), "%s", playlists[idx].title);
				cardTitle[s] = new GuiText(cardTitleStr[s], 18, white);
				cardTitle[s]->setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
				cardTitle[s]->setPosition(20, 14);
				cardTitle[s]->setMaxWidth(460);
				cardBtn[s]->setLabel(cardTitle[s], 0);

				snprintf(cardInfoStr[s], sizeof(cardInfoStr[s]), "%zu video%s", playlists[idx].items.size(), playlists[idx].items.size() == 1 ? "" : "s");
				cardInfo[s] = new GuiText(cardInfoStr[s], 15, grey);
				cardInfo[s]->setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
				cardInfo[s]->setPosition(20, 42);
				cardInfo[s]->setMaxWidth(460);
				cardBtn[s]->setLabel(cardInfo[s], 1);

				delImg[s] = new GuiImage(&btnOutline);
				delImg[s]->setSize(75, 34);
				delImgOver[s] = new GuiImage(&btnOutlineOver);
				delImgOver[s]->setSize(75, 34);
				delTxt[s] = new GuiText("Delete", 16, btnText);

				delBtn[s] = new GuiButton(75, 34);
				delBtn[s]->setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
				delBtn[s]->setPosition((sw - CARD_W) / 2 + CARD_W - 80, cy + (cardH - 34) / 2);
				delBtn[s]->setImage(delImg[s]);
				delBtn[s]->setImageOver(delImgOver[s]);
				delBtn[s]->setLabel(delTxt[s]);
				delBtn[s]->setSoundOver(&btnSoundOver);
				delBtn[s]->setTrigger(&trigA);
				delBtn[s]->setEffectGrow();

				plWin.append(cardBtn[s]);
				plWin.append(delBtn[s]);
			}
		}

		GuiImage prevImg(&btnOutline);
		prevImg.setSize(90, 36);
		GuiImage prevImgOver(&btnOutlineOver);
		prevImgOver.setSize(90, 36);
		GuiText prevTxt("Prev", 18, btnText);
		GuiButton prevBtn(90, 36);
		prevBtn.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
		prevBtn.setPosition(-70, 404);
		prevBtn.setImage(&prevImg);
		prevBtn.setImageOver(&prevImgOver);
		prevBtn.setLabel(&prevTxt);
		prevBtn.setSoundOver(&btnSoundOver);
		prevBtn.setTrigger(&trigA);
		prevBtn.setEffectGrow();

		GuiImage nextImg(&btnOutline);
		nextImg.setSize(90, 36);
		GuiImage nextImgOver(&btnOutlineOver);
		nextImgOver.setSize(90, 36);
		GuiText nextTxt("Next", 18, btnText);
		GuiButton nextBtn(90, 36);
		nextBtn.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
		nextBtn.setPosition(70, 404);
		nextBtn.setImage(&nextImg);
		nextBtn.setImageOver(&nextImgOver);
		nextBtn.setLabel(&nextTxt);
		nextBtn.setSoundOver(&btnSoundOver);
		nextBtn.setTrigger(&trigA);
		nextBtn.setEffectGrow();

		char pageStr[32];
		snprintf(pageStr, sizeof(pageStr), "%d / %d", page + 1, totalPages > 0 ? totalPages : 1);
		GuiText pageTxt(pageStr, 17, white);
		pageTxt.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
		pageTxt.setPosition(0, 412);

		if(totalPages > 1)
		{
			if(page > 0) plWin.append(&prevBtn);
			plWin.append(&pageTxt);
			if(page + 1 < totalPages) plWin.append(&nextBtn);
		}

		mainWindow->appendWithAutoRemove(&plWin);

		bool stayInPage = true;
		while(stayInPage && !platform->shouldExit())
		{
			if(!UpdateGui())
			{
				inMenu = false;
				break;
			}

			if(backBtn.getState() == STATE::CLICKED)
			{
				backBtn.resetState();
				inMenu = false;
				break;
			}

			if(newBtn.getState() == STATE::CLICKED)
			{
				newBtn.resetState();
				char newTitle[64] = "";
				if(EnterTextPrompt("New Playlist", "Create", newTitle, sizeof(newTitle)))
				{
					if(newTitle[0] != '\0')
					{
						ytCreateLocalPlaylist(newTitle);
						stayInPage = false;
						break;
					}
				}
			}

			for(int s = 0; s < visibleCount; s++)
			{
				int idx = startIdx + s;
				if(delBtn[s] && delBtn[s]->getState() == STATE::CLICKED)
				{
					delBtn[s]->resetState();
					int conf = WindowPrompt("Delete Playlist", "Delete this playlist?", "Delete", "Cancel");
					if(conf == 1)
					{
						ytDeleteLocalPlaylist(playlists[idx].id);
						stayInPage = false;
						break;
					}
				}

				if(cardBtn[s] && cardBtn[s]->getState() == STATE::CLICKED)
				{
					cardBtn[s]->resetState();
					mainWindow->remove(&plWin);
					std::string localId = std::string("local:") + playlists[idx].id;
					MenuPlaylist(localId.c_str(), playlists[idx].title);
					stayInPage = false;
					break;
				}
			}
			if(!stayInPage) break;

			if(totalPages > 1 && page > 0 && prevBtn.getState() == STATE::CLICKED)
			{
				prevBtn.resetState();
				page--;
				stayInPage = false;
				break;
			}

			if(totalPages > 1 && page + 1 < totalPages && nextBtn.getState() == STATE::CLICKED)
			{
				nextBtn.resetState();
				page++;
				stayInPage = false;
				break;
			}

			uint32_t pressed = 0;
			for(int i = 0; i < 4; i++)
				pressed |= controller[i]->getPadData().buttons_d;
			if(pressed & (INPUT_BTN_B | INPUT_BTN_1))
			{
				inMenu = false;
				break;
			}
		}

		mainWindow->remove(&plWin);

		for(int s = 0; s < CARDS_PER_PAGE; s++)
		{
			if(cardBtn[s]) delete cardBtn[s];
			if(cardBg[s]) delete cardBg[s];
			if(cardBgOver[s]) delete cardBgOver[s];
			if(cardTitle[s]) delete cardTitle[s];
			if(cardInfo[s]) delete cardInfo[s];
			if(delBtn[s]) delete delBtn[s];
			if(delImg[s]) delete delImg[s];
			if(delImgOver[s]) delete delImgOver[s];
			if(delTxt[s]) delete delTxt[s];
		}
	}
}

static void MenuBrewTube()
{
	const PixelColor white = {255, 255, 255, 255};
	const PixelColor grey = {170, 175, 190, 255};
	const PixelColor cardNormal = {26, 30, 42, 255};
	const PixelColor cardOver = {44, 52, 74, 255};
	const PixelColor btnText = {25, 28, 38, 255};

	int sw = platform->getVideo()->getScreenWidth();
	int sh = platform->getVideo()->getScreenHeight();

	GuiImageData btnOutline(button_png);
	GuiImageData btnOutlineOver(button_over_png);
	GuiSound btnSoundOver(button_over_pcm, button_over_pcm_size, SOUND::PCM);
	GuiTrigger trigA;
	trigA.setPrimaryTrigger();

	char currentQuery[128] = "";
	int queryLen = 0;
	std::unique_ptr<YtResult[]> currentResultsPtr(new YtResult[YT_MAX_RESULTS]);
	YtResult * currentResults = currentResultsPtr.get();
	int resultCount = 0;
	int resultPage = 0;
	char searchContinuation[1024] = "";
	bool hasMoreResults = false;

	GuiImageData * thumbData[YT_MAX_RESULTS] = { nullptr };
	GuiImage * thumbImg[YT_MAX_RESULTS] = { nullptr };

	auto clearThumbnails = [&]()
	{
		if(gThumbTask)
		{
			gThumbTask->stop = true;
			if(gThumbTask->thread.isRunning())
				gThumbTask->thread.join();
			for(int i = 0; i < YT_MAX_RESULTS; i++)
			{
				if(gThumbTask->textures[i])
				{
					platform->getVideo()->getImageRenderer()->destroyTexture(gThumbTask->textures[i]);
					gThumbTask->textures[i] = nullptr;
				}
			}
			delete gThumbTask;
			gThumbTask = nullptr;
		}

		for(int i = 0; i < YT_MAX_RESULTS; i++)
		{
			if(thumbImg[i]) { delete thumbImg[i]; thumbImg[i] = nullptr; }
			if(thumbData[i]) { delete thumbData[i]; thumbData[i] = nullptr; }
		}
	};

	enum ViewMode { VIEW_SEARCH, VIEW_RESULTS };
	ViewMode view = VIEW_SEARCH;
	bool keyboardActive = false;

	struct SuggestTask
	{
		Thread thread;
		char query[128];
		std::vector<std::string> results;
		volatile bool done = false;
		volatile bool stop = false;
	};

	SuggestTask * gSuggestTask = nullptr;
	std::vector<std::string> currentSuggestions;
	char lastSuggestQuery[128] = "";

	while(!platform->shouldExit())
	{
		if(view == VIEW_SEARCH)
		{
			GuiWindow searchWin(sw, sh);

			GuiImage headerBg(sw, 50, (PixelColor){10, 12, 18, 255});
			GuiImage headerLine(sw, 2, (PixelColor){0, 120, 215, 255});
			headerLine.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
			headerLine.setPosition(0, 50);

			GuiText titleTxt("BrewTube", 26, white);
			titleTxt.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
			titleTxt.setPosition(25, 10);

			GuiImage otherImg(&btnOutline);
			otherImg.setSize(90, 36);
			GuiImage otherImgOver(&btnOutlineOver);
			otherImgOver.setSize(90, 36);
			GuiText otherTxt("Other", 18, btnText);
			GuiButton otherBtn(90, 36);
			otherBtn.setAlignment(ALIGN_H::RIGHT, ALIGN_V::TOP);
			otherBtn.setPosition(-90, 7);
			otherBtn.setImage(&otherImg);
			otherBtn.setImageOver(&otherImgOver);
			otherBtn.setLabel(&otherTxt);
			otherBtn.setSoundOver(&btnSoundOver);
			otherBtn.setTrigger(&trigA);
			otherBtn.setEffectGrow();

			GuiImage exitImg(&btnOutline);
			exitImg.setSize(65, 36);
			GuiImage exitImgOver(&btnOutlineOver);
			exitImgOver.setSize(65, 36);
			GuiText exitTxt("Exit", 18, btnText);
			GuiButton exitBtn(65, 36);
			exitBtn.setAlignment(ALIGN_H::RIGHT, ALIGN_V::TOP);
			exitBtn.setPosition(-15, 7);
			exitBtn.setImage(&exitImg);
			exitBtn.setImageOver(&exitImgOver);
			exitBtn.setLabel(&exitTxt);
			exitBtn.setSoundOver(&btnSoundOver);
			exitBtn.setTrigger(&trigA);
			exitBtn.setEffectGrow();

			GuiImage resultsBtnImg(&btnOutline);
			resultsBtnImg.setSize(100, 36);
			GuiImage resultsBtnImgOver(&btnOutlineOver);
			resultsBtnImgOver.setSize(100, 36);
			GuiText resultsBtnTxt("Results", 18, btnText);
			GuiButton resultsBtn(100, 36);
			resultsBtn.setAlignment(ALIGN_H::RIGHT, ALIGN_V::TOP);
			resultsBtn.setPosition(-190, 7);
			resultsBtn.setImage(&resultsBtnImg);
			resultsBtn.setImageOver(&resultsBtnImgOver);
			resultsBtn.setLabel(&resultsBtnTxt);
			resultsBtn.setSoundOver(&btnSoundOver);
			resultsBtn.setTrigger(&trigA);
			resultsBtn.setEffectGrow();

			searchWin.append(&headerBg);
			searchWin.append(&headerLine);
			searchWin.append(&titleTxt);
			searchWin.append(&otherBtn);
			searchWin.append(&exitBtn);
			if(resultCount > 0)
				searchWin.append(&resultsBtn);

			GuiImage searchBarBg(590, 44, (PixelColor){28, 32, 45, 255});
			GuiImage searchBarBgOver(590, 44, (PixelColor){38, 44, 60, 255});
			GuiButton searchBarBtn(590, 44);
			searchBarBtn.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
			searchBarBtn.setPosition(0, 62);
			searchBarBtn.setImage(&searchBarBg);
			searchBarBtn.setImageOver(&searchBarBgOver);
			searchBarBtn.setSoundOver(&btnSoundOver);
			searchBarBtn.setTrigger(&trigA);

			GuiImage searchBarAccent(4, 44, (PixelColor){0, 120, 215, 255});
			searchBarAccent.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
			searchBarAccent.setPosition((sw - 590) / 2, 62);

			char displayQuery[140];
			if(queryLen == 0)
				snprintf(displayQuery, sizeof(displayQuery), "Search YouTube...");
			else
				snprintf(displayQuery, sizeof(displayQuery), "%s_", currentQuery);

			GuiText queryTxt(displayQuery, 20, queryLen == 0 ? (PixelColor){130, 135, 155, 255} : white);
			queryTxt.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
			queryTxt.setPosition((sw - 590) / 2 + 16, 72);
			queryTxt.setMaxWidth(560);

			searchWin.append(&searchBarBtn);
			searchWin.append(&searchBarAccent);
			searchWin.append(&queryTxt);

			static const char * const kbRow0 = "1234567890";
			static const char * const kbRow1 = "qwertyuiop";
			static const char * const kbRow2 = "asdfghjkl";
			static const char * const kbRow3 = "zxcvbnm-.";

			const int KEY_W = 48, KEY_H = 42, KEY_GAP = 6;
			const int MAX_KEYS = 40;

			GuiButton * keyBtn[MAX_KEYS] = { nullptr };
			GuiText * keyTxt[MAX_KEYS] = { nullptr };
			GuiImage * keyImg[MAX_KEYS] = { nullptr };
			GuiImage * keyImgOver[MAX_KEYS] = { nullptr };
			char keyChar[MAX_KEYS];
			int keyCount = 0;

			int barStartX = (sw - (10 * KEY_W + 9 * KEY_GAP)) / 2;
			int barY = 370;

			GuiImage clearImg(&btnOutline);
			clearImg.setSize(75, KEY_H);
			GuiImage clearImgOver(&btnOutlineOver);
			clearImgOver.setSize(75, KEY_H);
			GuiText clearTxt("Clear", 19, btnText);
			GuiButton clearBtn(75, KEY_H);
			clearBtn.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
			clearBtn.setPosition(barStartX, barY);
			clearBtn.setImage(&clearImg);
			clearBtn.setImageOver(&clearImgOver);
			clearBtn.setLabel(&clearTxt);
			clearBtn.setSoundOver(&btnSoundOver);
			clearBtn.setTrigger(&trigA);
			clearBtn.setEffectGrow();

			GuiImage delImg(&btnOutline);
			delImg.setSize(75, KEY_H);
			GuiImage delImgOver(&btnOutlineOver);
			delImgOver.setSize(75, KEY_H);
			GuiText delTxt("Del", 19, btnText);
			GuiButton delBtn(75, KEY_H);
			delBtn.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
			delBtn.setPosition(barStartX + 83, barY);
			delBtn.setImage(&delImg);
			delBtn.setImageOver(&delImgOver);
			delBtn.setLabel(&delTxt);
			delBtn.setSoundOver(&btnSoundOver);
			delBtn.setTrigger(&trigA);
			delBtn.setEffectGrow();

			GuiImage spaceImg(&btnOutline);
			spaceImg.setSize(202, KEY_H);
			GuiImage spaceImgOver(&btnOutlineOver);
			spaceImgOver.setSize(202, KEY_H);
			GuiText spaceTxt("Space", 19, btnText);
			GuiButton spaceBtn(202, KEY_H);
			spaceBtn.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
			spaceBtn.setPosition(barStartX + 166, barY);
			spaceBtn.setImage(&spaceImg);
			spaceBtn.setImageOver(&spaceImgOver);
			spaceBtn.setLabel(&spaceTxt);
			spaceBtn.setSoundOver(&btnSoundOver);
			spaceBtn.setTrigger(&trigA);
			spaceBtn.setEffectGrow();

			GuiImage searchImg(&btnOutline);
			searchImg.setSize(158, KEY_H);
			GuiImage searchImgOver(&btnOutlineOver);
			searchImgOver.setSize(158, KEY_H);
			GuiText searchTxt("Search", 20, btnText);
			GuiButton searchBtn(158, KEY_H);
			searchBtn.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
			searchBtn.setPosition(barStartX + 376, barY);
			searchBtn.setImage(&searchImg);
			searchBtn.setImageOver(&searchImgOver);
			searchBtn.setLabel(&searchTxt);
			searchBtn.setSoundOver(&btnSoundOver);
			searchBtn.setTrigger(&trigA);
			searchBtn.setEffectGrow();

			auto addKeyRow = [&](const char * chars, int rowY)
			{
				int cols = strlen(chars);
				int rowW = cols * KEY_W + (cols - 1) * KEY_GAP;
				int startX = (sw - rowW) / 2;

				for(int c = 0; c < cols; c++)
				{
					int i = keyCount++;
					keyChar[i] = chars[c];
					char lbl[2] = { keyChar[i], '\0' };

					keyImg[i] = new GuiImage(&btnOutline);
					keyImg[i]->setSize(KEY_W, KEY_H);
					keyImgOver[i] = new GuiImage(&btnOutlineOver);
					keyImgOver[i]->setSize(KEY_W, KEY_H);
					keyTxt[i] = new GuiText(lbl, 21, btnText);

					keyBtn[i] = new GuiButton(KEY_W, KEY_H);
					keyBtn[i]->setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
					keyBtn[i]->setPosition(startX + c * (KEY_W + KEY_GAP), rowY);
					keyBtn[i]->setImage(keyImg[i]);
					keyBtn[i]->setImageOver(keyImgOver[i]);
					keyBtn[i]->setLabel(keyTxt[i]);
					keyBtn[i]->setSoundOver(&btnSoundOver);
					keyBtn[i]->setTrigger(&trigA);
					keyBtn[i]->setEffectGrow();

					searchWin.append(keyBtn[i]);
				}
			};

			struct CategoryItem
			{
				const char * title;
				const char * browseId;
				const uint8_t * iconPng;
				size_t iconSize;
				bool isSubsFeed;
			};

			std::vector<CategoryItem> categories;
			if(!ytGetSubscriptions().empty())
				categories.push_back({ "Local Subscriptionsfeed", "", icon_subs_png, icon_subs_png_size, true });
			categories.push_back({ "Hype Leaderboard", "FEhype_leaderboard", icon_hype_png, icon_hype_png_size, false });
			categories.push_back({ "Music", "UC-9-kyTW8ZkZNDHQJ6FgpwQ", icon_music_png, icon_music_png_size, false });
			categories.push_back({ "Gaming", "UCOpNcN46UbXVtpKMrmU4Abg", icon_gaming_png, icon_gaming_png_size, false });
			categories.push_back({ "News", "UCYfdidRxbB8Qhf0Nx7ioOYw", icon_news_png, icon_news_png_size, false });
			categories.push_back({ "Sports", "UCEgdi0XIXXZ-qJOFPf4JSKw", icon_sports_png, icon_sports_png_size, false });

			const int catCount = (int)categories.size();
			const int CAT_W = 286, CAT_H = 64;
			int catStartX = (sw - (2 * CAT_W + 18)) / 2;

			GuiButton * catBtn[6] = { nullptr };
			GuiImage * catCardBg[6] = { nullptr };
			GuiImage * catCardBgOver[6] = { nullptr };
			GuiImageData * catIconData[6] = { nullptr };
			GuiImage * catIconImg[6] = { nullptr };
			GuiText * catTitleTxt[6] = { nullptr };

			const int MAX_SUGGEST_BTNS = 6;
			GuiButton * suggBtn[MAX_SUGGEST_BTNS] = { nullptr };
			GuiImage * suggImg[MAX_SUGGEST_BTNS] = { nullptr };
			GuiImage * suggImgOver[MAX_SUGGEST_BTNS] = { nullptr };
			GuiText * suggTxt[MAX_SUGGEST_BTNS] = { nullptr };
			char suggLabels[MAX_SUGGEST_BTNS][64];

			if(keyboardActive)
			{
				if(!gSuggestTask && strcmp(currentQuery, lastSuggestQuery) != 0)
				{
					snprintf(lastSuggestQuery, sizeof(lastSuggestQuery), "%s", currentQuery);

					if(queryLen > 0)
					{
						gSuggestTask = new SuggestTask();
						snprintf(gSuggestTask->query, sizeof(gSuggestTask->query), "%s", currentQuery);
						gSuggestTask->thread.start([](void * arg) -> void * {
							SuggestTask * t = static_cast<SuggestTask *>(arg);
							ytFetchSearchSuggestions(t->query, t->results, 6);
							t->done = true;
							return nullptr;
						}, gSuggestTask, 32 * 1024, ThreadPriority::Normal);
					}
					else
					{
						currentSuggestions.clear();
					}
				}

				if(gSuggestTask && gSuggestTask->done)
				{
					if(gSuggestTask->thread.isRunning())
						gSuggestTask->thread.join();
					currentSuggestions = gSuggestTask->results;
					delete gSuggestTask;
					gSuggestTask = nullptr;
				}

				if(!currentSuggestions.empty())
				{
					int sCount = (int)currentSuggestions.size();
					if(sCount > MAX_SUGGEST_BTNS) sCount = MAX_SUGGEST_BTNS;
					for(int i = 0; i < sCount; i++)
					{
						int sRow = i / 3;
						int sCol = i % 3;
						int sx = (sw - 590) / 2 + sCol * 200;
						int sy = 112 + sRow * 32;

						suggImg[i] = new GuiImage(&btnOutline);
						suggImg[i]->setSize(190, 26);
						suggImgOver[i] = new GuiImage(&btnOutlineOver);
						suggImgOver[i]->setSize(190, 26);

						snprintf(suggLabels[i], sizeof(suggLabels[i]), "%s", currentSuggestions[i].c_str());
						suggTxt[i] = new GuiText(suggLabels[i], 15, btnText);
						suggTxt[i]->setMaxWidth(180);

						suggBtn[i] = new GuiButton(190, 26);
						suggBtn[i]->setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
						suggBtn[i]->setPosition(sx, sy);
						suggBtn[i]->setImage(suggImg[i]);
						suggBtn[i]->setImageOver(suggImgOver[i]);
						suggBtn[i]->setLabel(suggTxt[i]);
						suggBtn[i]->setSoundOver(&btnSoundOver);
						suggBtn[i]->setTrigger(&trigA);
						suggBtn[i]->setEffectGrow();

						searchWin.append(suggBtn[i]);
					}
				}

				addKeyRow(kbRow0, 178);
				addKeyRow(kbRow1, 226);
				addKeyRow(kbRow2, 274);
				addKeyRow(kbRow3, 322);

				searchWin.append(&clearBtn);
				searchWin.append(&delBtn);
				searchWin.append(&spaceBtn);
				searchWin.append(&searchBtn);
			}
			else
			{
				lastSuggestQuery[0] = '\0';
				currentSuggestions.clear();
				for(int i = 0; i < catCount; i++)
				{
					int col = i % 2;
					int row = i / 2;
					int cx = catStartX + col * (CAT_W + 18);
					int cy = 126 + row * (CAT_H + 14);

					catCardBg[i] = new GuiImage(CAT_W, CAT_H, cardNormal);
					catCardBgOver[i] = new GuiImage(CAT_W, CAT_H, cardOver);

					catBtn[i] = new GuiButton(CAT_W, CAT_H);
					catBtn[i]->setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
					catBtn[i]->setPosition(cx, cy);
					catBtn[i]->setImage(catCardBg[i]);
					catBtn[i]->setImageOver(catCardBgOver[i]);
					catBtn[i]->setSoundOver(&btnSoundOver);
					catBtn[i]->setTrigger(&trigA);
					catBtn[i]->setEffectGrow();

					catIconData[i] = new GuiImageData(categories[i].iconPng, categories[i].iconSize);
					catIconImg[i] = new GuiImage(catIconData[i]);
					catIconImg[i]->setSize(32, 32);
					catIconImg[i]->setAlignment(ALIGN_H::LEFT, ALIGN_V::MIDDLE);
					catIconImg[i]->setPosition(16, 0);
					catBtn[i]->setIcon(catIconImg[i]);

					catTitleTxt[i] = new GuiText(categories[i].title, 18, white);
					catTitleTxt[i]->setAlignment(ALIGN_H::LEFT, ALIGN_V::MIDDLE);
					catTitleTxt[i]->setPosition(60, 0);
					catTitleTxt[i]->setMaxWidth(CAT_W - 70);
					catBtn[i]->setLabel(catTitleTxt[i]);

					searchWin.append(catBtn[i]);
				}
			}

			GuiText updateTxt("Update available. Download the latest version from github.com/ReviveMii/brewtube", 12, (PixelColor){200, 200, 80, 255});
			updateTxt.setAlignment(ALIGN_H::LEFT, ALIGN_V::BOTTOM);
			updateTxt.setPosition(20, -6);
			bool updateTxtAdded = false;

			mainWindow->appendWithAutoRemove(&searchWin);

			bool stayInSearch = true;
			while(stayInSearch && !platform->shouldExit())
			{
				if(!UpdateGui())
					return;

				if(!updateTxtAdded && ytIsUpdateAvailable())
				{
					updateTxtAdded = true;
					searchWin.append(&updateTxt);
				}

				if(searchBarBtn.getState() == STATE::CLICKED)
				{
					searchBarBtn.resetState();
					if(!keyboardActive)
					{
						keyboardActive = true;
						stayInSearch = false;
						break;
					}
				}

				if(keyboardActive && gSuggestTask && gSuggestTask->done)
				{
					stayInSearch = false;
					break;
				}

				bool doSearch = false;
				if(keyboardActive)
				{
					for(int i = 0; i < MAX_SUGGEST_BTNS; i++)
					{
						if(suggBtn[i] && suggBtn[i]->getState() == STATE::CLICKED)
						{
							suggBtn[i]->resetState();
							if(i < (int)currentSuggestions.size())
							{
								snprintf(currentQuery, sizeof(currentQuery), "%s", currentSuggestions[i].c_str());
								queryLen = strlen(currentQuery);
								snprintf(displayQuery, sizeof(displayQuery), "%s_", currentQuery);
								queryTxt.setText(displayQuery);
								queryTxt.setColor(white);
								doSearch = true;
							}
						}
					}

					bool queryChanged = false;
					for(int i = 0; i < keyCount; i++)
					{
						if(keyBtn[i]->getState() == STATE::CLICKED)
						{
							keyBtn[i]->resetState();
							if(queryLen < 127)
							{
								currentQuery[queryLen++] = keyChar[i];
								currentQuery[queryLen] = '\0';
								snprintf(displayQuery, sizeof(displayQuery), "%s_", currentQuery);
								queryTxt.setText(displayQuery);
								queryTxt.setColor(white);
								queryChanged = true;
							}
						}
					}

					if(spaceBtn.getState() == STATE::CLICKED)
					{
						spaceBtn.resetState();
						if(queryLen < 127)
						{
							currentQuery[queryLen++] = ' ';
							currentQuery[queryLen] = '\0';
							snprintf(displayQuery, sizeof(displayQuery), "%s_", currentQuery);
							queryTxt.setText(displayQuery);
							queryTxt.setColor(white);
							queryChanged = true;
						}
					}

					if(delBtn.getState() == STATE::CLICKED)
					{
						delBtn.resetState();
						if(queryLen > 0)
						{
							currentQuery[--queryLen] = '\0';
							if(queryLen == 0)
							{
								snprintf(displayQuery, sizeof(displayQuery), "Search YouTube...");
								queryTxt.setText(displayQuery);
								queryTxt.setColor((PixelColor){130, 135, 155, 255});
							}
							else
							{
								snprintf(displayQuery, sizeof(displayQuery), "%s_", currentQuery);
								queryTxt.setText(displayQuery);
								queryTxt.setColor(white);
							}
							queryChanged = true;
						}
					}

					if(clearBtn.getState() == STATE::CLICKED)
					{
						clearBtn.resetState();
						queryLen = 0;
						currentQuery[0] = '\0';
						snprintf(displayQuery, sizeof(displayQuery), "Search YouTube...");
						queryTxt.setText(displayQuery);
						queryTxt.setColor((PixelColor){130, 135, 155, 255});
						queryChanged = true;
					}

					if(queryChanged && !doSearch)
					{
						stayInSearch = false;
						break;
					}
				}
				else
				{
					for(int i = 0; i < catCount; i++)
					{
						if(catBtn[i] && catBtn[i]->getState() == STATE::CLICKED)
						{
							catBtn[i]->resetState();

							struct CatBrowseTask
							{
								char browseId[64];
								bool isSubs;
								YtResult * results;
								int maxResults;
								char error[128];
								int count = 0;
								volatile bool done = false;
							} cTask;

							snprintf(cTask.browseId, sizeof(cTask.browseId), "%s", categories[i].browseId);
							cTask.isSubs = categories[i].isSubsFeed;
							cTask.results = currentResults;
							cTask.maxResults = YT_MAX_RESULTS;
							cTask.error[0] = '\0';
							cTask.done = false;

							Thread cThread;
							cThread.start([](void * arg) -> void * {
								CatBrowseTask * t = static_cast<CatBrowseTask *>(arg);
								if(t->isSubs)
									t->count = ytFetchSubscriptionsFeed(t->results, t->maxResults, t->error, sizeof(t->error));
								else
									t->count = ytBrowseCategory(t->browseId, t->results, t->maxResults, t->error, sizeof(t->error));
								t->done = true;
								return nullptr;
							}, &cTask, 64 * 1024, ThreadPriority::Normal);

							char loadMsg[64];
							snprintf(loadMsg, sizeof(loadMsg), "Loading %s...", categories[i].title);
							RunWithLoadingScreen("BrewTube", loadMsg, cTask.done);
							cThread.join();

							if(cTask.count <= 0)
							{
								WindowPrompt("Browse Failed", cTask.error[0] ? cTask.error : "No items found", "OK", nullptr);
							}
							else
							{
								clearThumbnails();
								snprintf(currentQuery, sizeof(currentQuery), "%s", categories[i].title);
								resultCount = cTask.count;
								resultPage = 0;
								searchContinuation[0] = '\0';
								hasMoreResults = false;
								startAsyncThumbnails(currentResults, resultCount);

								view = VIEW_RESULTS;
								stayInSearch = false;
								break;
							}
						}
					}
					if(!stayInSearch) break;
				}

				uint32_t pressed = 0;
				for(int i = 0; i < 4; i++)
					pressed |= controller[i]->getPadData().buttons_d;

				if(keyboardActive)
				{
					for(int i = 0; i < 4; i++)
					{
						const InputPadData &pad = controller[i]->getPadData();
						if(pad.validPointer && (pad.buttons_d & INPUT_BTN_A))
						{
							if(pad.cursor_y > barY + KEY_H)
							{
								keyboardActive = false;
								stayInSearch = false;
								break;
							}
						}
					}
					if(!stayInSearch) break;
				}

				if(pressed & INPUT_BTN_B)
				{
					if(queryLen > 0)
					{
						currentQuery[--queryLen] = '\0';
						if(queryLen == 0)
						{
							snprintf(displayQuery, sizeof(displayQuery), "Search YouTube...");
							queryTxt.setText(displayQuery);
							queryTxt.setColor((PixelColor){130, 135, 155, 255});
						}
						else
						{
							snprintf(displayQuery, sizeof(displayQuery), "%s_", currentQuery);
							queryTxt.setText(displayQuery);
							queryTxt.setColor(white);
						}
					}
					else if(keyboardActive)
					{
						keyboardActive = false;
						stayInSearch = false;
						break;
					}
				}

				if(otherBtn.getState() == STATE::CLICKED)
				{
					otherBtn.resetState();
					int otherChoice = ShowOtherMenuPrompt();
					if(otherChoice == 1)
					{
						int res = MenuBrowseFiles();
						if(res == MENU_EXIT) return;
					}
					else if(otherChoice == 2)
					{
						char streamUrl[512];
						if(EnterUrlPrompt(streamUrl, sizeof(streamUrl)))
						{
							char error[128] = "";
							HaltDeviceCheckingThread();
							mainWindow->setState(STATE::DISABLED);
							PlayResult playRes = PlayFile(streamUrl, error, sizeof(error));
							mainWindow->setState(STATE::DEFAULT);
							ResumeDeviceCheckingThread();
							if(playRes == PLAY_EXIT) return;
							if(playRes == PLAY_ERROR) WindowPrompt("Error", error, "OK", nullptr);
						}
					}
					else if(otherChoice == 3)
					{
						mainWindow->remove(&searchWin);
						MenuSubscriptions();
						stayInSearch = false;
						break;
					}
					else if(otherChoice == 4)
					{
						mainWindow->remove(&searchWin);
						MenuLocalPlaylists();
						stayInSearch = false;
						break;
					}
				}

				if(exitBtn.getState() == STATE::CLICKED)
				{
					platform->triggerExit();
					return;
				}

				if(resultCount > 0 && resultsBtn.getState() == STATE::CLICKED)
				{
					resultsBtn.resetState();
					view = VIEW_RESULTS;
					stayInSearch = false;
				}

				if(doSearch || (keyboardActive && searchBtn.getState() == STATE::CLICKED) || (pressed & INPUT_BTN_PLUS))
				{
					if(keyboardActive) searchBtn.resetState();
					if(queryLen > 0)
					{
						struct SearchTask
						{
							char query[128];
							YtResult * results;
							int maxResults;
							char error[128];
							char nextContinuation[1024];
							int count = 0;
							volatile bool done = false;
						} sTask;

						snprintf(sTask.query, sizeof(sTask.query), "%s", currentQuery);
						sTask.results = currentResults;
						sTask.maxResults = 12;
						sTask.error[0] = '\0';
						sTask.nextContinuation[0] = '\0';
						sTask.done = false;

						Thread searchThread;
						searchThread.start([](void * arg) -> void * {
							SearchTask * t = static_cast<SearchTask *>(arg);
							t->count = ytSearch(t->query, t->results, t->maxResults, t->error, sizeof(t->error),
								nullptr, t->nextContinuation, sizeof(t->nextContinuation));
							t->done = true;
							return nullptr;
						}, &sTask, 64 * 1024, ThreadPriority::Normal);

						RunWithLoadingScreen("BrewTube", "Searching YouTube", sTask.done);
						searchThread.join();

						if(sTask.count <= 0)
						{
							WindowPrompt("Search Failed", sTask.error[0] ? sTask.error : "No videos found", "OK", nullptr);
						}
						else
						{
							clearThumbnails();
							resultCount = sTask.count;
							resultPage = 0;
							snprintf(searchContinuation, sizeof(searchContinuation), "%s", sTask.nextContinuation);
							hasMoreResults = searchContinuation[0] != '\0';
							startAsyncThumbnails(currentResults, resultCount);

							view = VIEW_RESULTS;
							stayInSearch = false;
						}
					}
				}
			}

			if(keyboardActive)
			{
				for(int i = 0; i < MAX_SUGGEST_BTNS; i++)
				{
					delete suggBtn[i];
					delete suggImg[i];
					delete suggImgOver[i];
					delete suggTxt[i];
				}
				for(int i = 0; i < keyCount; i++)
				{
					delete keyBtn[i];
					delete keyTxt[i];
					delete keyImg[i];
					delete keyImgOver[i];
				}
			}
			else
			{
				for(int i = 0; i < catCount; i++)
				{
					delete catBtn[i];
					delete catCardBg[i];
					delete catCardBgOver[i];
					delete catIconImg[i];
					delete catIconData[i];
					delete catTitleTxt[i];
				}
			}
		}
		else if(view == VIEW_RESULTS)
		{
			GuiWindow resultsWin(sw, sh);

			GuiImage headerBg(sw, 50, (PixelColor){10, 12, 18, 255});
			GuiImage headerLine(sw, 2, (PixelColor){0, 120, 215, 255});
			headerLine.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
			headerLine.setPosition(0, 50);

			GuiText titleTxt("BrewTube", 26, white);
			titleTxt.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
			titleTxt.setPosition(25, 10);

			GuiImage newSearchImg(&btnOutline);
			newSearchImg.setSize(120, 36);
			GuiImage newSearchImgOver(&btnOutlineOver);
			newSearchImgOver.setSize(120, 36);
			GuiText newSearchTxt("New Search", 18, btnText);
			GuiButton newSearchBtn(120, 36);
			newSearchBtn.setAlignment(ALIGN_H::RIGHT, ALIGN_V::TOP);
			newSearchBtn.setPosition(-190, 7);
			newSearchBtn.setImage(&newSearchImg);
			newSearchBtn.setImageOver(&newSearchImgOver);
			newSearchBtn.setLabel(&newSearchTxt);
			newSearchBtn.setSoundOver(&btnSoundOver);
			newSearchBtn.setTrigger(&trigA);
			newSearchBtn.setEffectGrow();

			GuiImage otherImg(&btnOutline);
			otherImg.setSize(90, 36);
			GuiImage otherImgOver(&btnOutlineOver);
			otherImgOver.setSize(90, 36);
			GuiText otherTxt("Other", 18, btnText);
			GuiButton otherBtn(90, 36);
			otherBtn.setAlignment(ALIGN_H::RIGHT, ALIGN_V::TOP);
			otherBtn.setPosition(-90, 7);
			otherBtn.setImage(&otherImg);
			otherImgOver.setSize(90, 36);
			otherBtn.setImageOver(&otherImgOver);
			otherBtn.setLabel(&otherTxt);
			otherBtn.setSoundOver(&btnSoundOver);
			otherBtn.setTrigger(&trigA);
			otherBtn.setEffectGrow();

			GuiImage exitImg(&btnOutline);
			exitImg.setSize(65, 36);
			GuiImage exitImgOver(&btnOutlineOver);
			exitImgOver.setSize(65, 36);
			GuiText exitTxt("Exit", 18, btnText);
			GuiButton exitBtn(65, 36);
			exitBtn.setAlignment(ALIGN_H::RIGHT, ALIGN_V::TOP);
			exitBtn.setPosition(-15, 7);
			exitBtn.setImage(&exitImg);
			exitBtn.setImageOver(&exitImgOver);
			exitBtn.setLabel(&exitTxt);
			exitBtn.setSoundOver(&btnSoundOver);
			exitBtn.setTrigger(&trigA);
			exitBtn.setEffectGrow();

			resultsWin.append(&headerBg);
			resultsWin.append(&headerLine);
			resultsWin.append(&titleTxt);
			resultsWin.append(&newSearchBtn);
			resultsWin.append(&otherBtn);
			resultsWin.append(&exitBtn);

			char summary[160];
			snprintf(summary, sizeof(summary), "Results for \"%s\"", currentQuery);
			GuiText summaryTxt(summary, 16, grey);
			summaryTxt.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
			summaryTxt.setPosition(25, 56);
			resultsWin.append(&summaryTxt);

			const int CARDS_PER_PAGE = 3;
			const int CARD_W = 590, CARD_H = 100;
			const int CARD_Y_START = 78;
			const int CARD_GAP = 10;

			GuiButton * cardBtn[CARDS_PER_PAGE] = { nullptr };
			GuiButton * cardAuthorBtn[CARDS_PER_PAGE] = { nullptr };
			GuiImage * cardBg[CARDS_PER_PAGE] = { nullptr };
			GuiImage * cardBgOver[CARDS_PER_PAGE] = { nullptr };
			GuiText * cardTitle[CARDS_PER_PAGE] = { nullptr };
			GuiText * cardAuthor[CARDS_PER_PAGE] = { nullptr };
			GuiText * cardMeta[CARDS_PER_PAGE] = { nullptr };
			char cardTitleStr[CARDS_PER_PAGE][64];
			char cardAuthorStr[CARDS_PER_PAGE][96];
			char cardMetaStr[CARDS_PER_PAGE][96];

			int startIdx = resultPage * CARDS_PER_PAGE;
			int visibleCount = 0;

			for(int s = 0; s < CARDS_PER_PAGE; s++)
			{
				int idx = startIdx + s;
				if(idx >= resultCount) break;

				visibleCount++;
				int cardY = CARD_Y_START + s * (CARD_H + CARD_GAP);

				cardBg[s] = new GuiImage(CARD_W, CARD_H, cardNormal);
				cardBgOver[s] = new GuiImage(CARD_W, CARD_H, cardOver);

				cardBtn[s] = new GuiButton(CARD_W, CARD_H);
				cardBtn[s]->setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
				cardBtn[s]->setPosition(0, cardY);
				cardBtn[s]->setImage(cardBg[s]);
				cardBtn[s]->setImageOver(cardBgOver[s]);
				cardBtn[s]->setSoundOver(&btnSoundOver);
				cardBtn[s]->setTrigger(&trigA);
				cardBtn[s]->setEffectGrow();

				if(thumbImg[idx])
				{
					thumbImg[idx]->setAlignment(ALIGN_H::LEFT, ALIGN_V::MIDDLE);
					thumbImg[idx]->setPosition(10, 0);
					cardBtn[s]->setIcon(thumbImg[idx]);
				}

				snprintf(cardTitleStr[s], sizeof(cardTitleStr[s]), "%s", currentResults[idx].title);
				if(strlen(currentResults[idx].title) > 42)
				{
					cardTitleStr[s][39] = '.';
					cardTitleStr[s][40] = '.';
					cardTitleStr[s][41] = '.';
					cardTitleStr[s][42] = '\0';
				}
				cardTitle[s] = new GuiText(cardTitleStr[s], 18, white);
				cardTitle[s]->setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
				cardTitle[s]->setPosition(168, 14);
				cardTitle[s]->setMaxWidth(415);
				cardBtn[s]->setLabel(cardTitle[s], 0);

				if(currentResults[idx].isChannel)
				{
					snprintf(cardAuthorStr[s], sizeof(cardAuthorStr[s]), "Channel");
					cardAuthor[s] = new GuiText(cardAuthorStr[s], 15, grey);
					cardAuthor[s]->setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
					cardAuthor[s]->setPosition(168, 42);
					cardAuthor[s]->setMaxWidth(415);
					cardBtn[s]->setLabel(cardAuthor[s], 1);

					if(currentResults[idx].author[0] != '\0' && currentResults[idx].viewCountText[0] != '\0')
						snprintf(cardMetaStr[s], sizeof(cardMetaStr[s]), "%s   •   %s", currentResults[idx].author, currentResults[idx].viewCountText);
					else if(currentResults[idx].author[0] != '\0')
						snprintf(cardMetaStr[s], sizeof(cardMetaStr[s]), "%s", currentResults[idx].author);
					else
						snprintf(cardMetaStr[s], sizeof(cardMetaStr[s]), "%s", currentResults[idx].viewCountText);
				}
				else if(currentResults[idx].isPlaylist)
				{
					snprintf(cardAuthorStr[s], sizeof(cardAuthorStr[s]), "%s", currentResults[idx].author[0] ? currentResults[idx].author : "YouTube");
					cardAuthor[s] = new GuiText(cardAuthorStr[s], 15, grey);
					cardAuthor[s]->setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
					cardAuthor[s]->setPosition(168, 42);
					cardAuthor[s]->setMaxWidth(415);
					cardBtn[s]->setLabel(cardAuthor[s], 1);

					if(currentResults[idx].lengthText[0] != '\0' && strcmp(currentResults[idx].lengthText, "Playlist") != 0)
						snprintf(cardMetaStr[s], sizeof(cardMetaStr[s]), "Playlist   •   %s", currentResults[idx].lengthText);
					else
						snprintf(cardMetaStr[s], sizeof(cardMetaStr[s]), "Playlist");
				}
				else
				{
					snprintf(cardAuthorStr[s], sizeof(cardAuthorStr[s]), "%s", currentResults[idx].author);
					cardAuthor[s] = new GuiText(cardAuthorStr[s], 15, grey);
					cardAuthor[s]->setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
					cardAuthor[s]->setPosition(168, 42);
					cardAuthor[s]->setMaxWidth(415);
					cardBtn[s]->setLabel(cardAuthor[s], 1);

					if(currentResults[idx].author[0] != '\0')
					{
						int authorW = cardAuthor[s]->getTextWidth();
						if(authorW > 240) authorW = 240;
						if(authorW < 20) authorW = 20;

						cardAuthorBtn[s] = new GuiButton(authorW, 18);
						cardAuthorBtn[s]->setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
						cardAuthorBtn[s]->setPosition((sw - CARD_W) / 2 + 168, cardY + 41);
						cardAuthorBtn[s]->setTrigger(&trigA);
						cardAuthorBtn[s]->setSoundOver(&btnSoundOver);
					}

					char *metaPtr = cardMetaStr[s];
					size_t metaRem = sizeof(cardMetaStr[s]);
					cardMetaStr[s][0] = '\0';

					const char *fields[] = {
						currentResults[idx].lengthText,
						currentResults[idx].viewCountText,
						currentResults[idx].publishedText
					};
					bool first = true;
					for(const char *f : fields)
					{
						if(f && f[0] != '\0')
						{
							int written = snprintf(metaPtr, metaRem, "%s%s", first ? "" : "   •   ", f);
							if(written > 0 && (size_t)written < metaRem)
							{
								metaPtr += written;
								metaRem -= written;
							}
							first = false;
						}
					}
				}
				cardMeta[s] = new GuiText(cardMetaStr[s], 14, (PixelColor){135, 145, 170, 255});
				cardMeta[s]->setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
				cardMeta[s]->setPosition(168, 68);
				cardMeta[s]->setMaxWidth(415);
				cardBtn[s]->setLabel(cardMeta[s], 2);

				resultsWin.append(cardBtn[s]);
				if(cardAuthorBtn[s]) resultsWin.append(cardAuthorBtn[s]);
			}

			int totalPages = (resultCount + CARDS_PER_PAGE - 1) / CARDS_PER_PAGE;
			GuiImage prevImg(&btnOutline);
			prevImg.setSize(90, 36);
			GuiImage prevImgOver(&btnOutlineOver);
			prevImgOver.setSize(90, 36);
			GuiText prevTxt("Prev", 18, btnText);
			GuiButton prevBtn(90, 36);
			prevBtn.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
			prevBtn.setPosition(-70, 418);
			prevBtn.setImage(&prevImg);
			prevBtn.setImageOver(&prevImgOver);
			prevBtn.setLabel(&prevTxt);
			prevBtn.setSoundOver(&btnSoundOver);
			prevBtn.setTrigger(&trigA);
			prevBtn.setEffectGrow();

			GuiImage nextImg(&btnOutline);
			nextImg.setSize(90, 36);
			GuiImage nextImgOver(&btnOutlineOver);
			nextImgOver.setSize(90, 36);
			GuiText nextTxt("Next", 18, btnText);
			GuiButton nextBtn(90, 36);
			nextBtn.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
			nextBtn.setPosition(70, 418);
			nextBtn.setImage(&nextImg);
			nextBtn.setImageOver(&nextImgOver);
			nextBtn.setLabel(&nextTxt);
			nextBtn.setSoundOver(&btnSoundOver);
			nextBtn.setTrigger(&trigA);
			nextBtn.setEffectGrow();

			char pageStr[32];
			snprintf(pageStr, sizeof(pageStr), "%d / %d", resultPage + 1, totalPages > 0 ? totalPages : 1);
			GuiText pageTxt(pageStr, 17, white);
			pageTxt.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
			pageTxt.setPosition(0, 426);

			bool canGoNext = (resultPage + 1 < totalPages) || (resultPage + 1 == totalPages && hasMoreResults && resultCount < YT_MAX_RESULTS);

			if(totalPages > 1 || canGoNext)
			{
				if(resultPage > 0) resultsWin.append(&prevBtn);
				resultsWin.append(&pageTxt);
				if(canGoNext) resultsWin.append(&nextBtn);
			}

			mainWindow->appendWithAutoRemove(&resultsWin);

			auto loadMoreResults = [&]() {
				if(resultPage + 1 >= totalPages && hasMoreResults && resultCount < YT_MAX_RESULTS)
				{
					struct MoreTask
					{
						char continuation[1024];
						YtResult * results;
						int maxResults;
						char error[128];
						char nextContinuation[1024];
						int count = 0;
						volatile bool done = false;
					} mTask;

					snprintf(mTask.continuation, sizeof(mTask.continuation), "%s", searchContinuation);
					mTask.results = currentResults + resultCount;
					mTask.maxResults = YT_MAX_RESULTS - resultCount;
					mTask.error[0] = '\0';
					mTask.nextContinuation[0] = '\0';
					mTask.done = false;

					Thread moreThread;
					moreThread.start([](void * arg) -> void * {
						MoreTask * t = static_cast<MoreTask *>(arg);
						t->count = ytSearch(nullptr, t->results, t->maxResults, t->error, sizeof(t->error),
							t->continuation, t->nextContinuation, sizeof(t->nextContinuation));
						t->done = true;
						return nullptr;
					}, &mTask, 64 * 1024, ThreadPriority::Normal);

					RunWithLoadingScreen("BrewTube", "Loading more videos", mTask.done);
					moreThread.join();

					if(mTask.count > 0)
					{
						resultCount += mTask.count;
						snprintf(searchContinuation, sizeof(searchContinuation), "%s", mTask.nextContinuation);
						hasMoreResults = searchContinuation[0] != '\0';
						startAsyncThumbnails(currentResults, resultCount);
					}
					else
					{
						hasMoreResults = false;
					}
				}
				resultPage++;
			};

			bool stayInResults = true;
			while(stayInResults && !platform->shouldExit())
			{
				if(!UpdateGui())
					return;

				if(gThumbTask)
				{
					gThumbTask->lock.lock();
					for(int i = 0; i < resultCount; i++)
					{
						if(gThumbTask->ready[i] && !thumbImg[i] && gThumbTask->textures[i])
						{
							thumbData[i] = new GuiImageData(gThumbTask->textures[i], gThumbTask->widths[i], gThumbTask->heights[i]);
							gThumbTask->textures[i] = nullptr;
							thumbImg[i] = new GuiImage(thumbData[i]);
							if(currentResults[i].isChannel)
								thumbImg[i]->setSize(70, 70);
							else
								thumbImg[i]->setSize(144, 81);
							thumbImg[i]->setAlignment(ALIGN_H::LEFT, ALIGN_V::MIDDLE);
							thumbImg[i]->setPosition(10, 0);

							for(int s = 0; s < visibleCount; s++)
							{
								if(startIdx + s == i && cardBtn[s])
								{
									cardBtn[s]->setIcon(thumbImg[i]);
								}
							}
						}
					}
					gThumbTask->lock.unlock();
				}

				for(int s = 0; s < visibleCount; s++)
				{
					if(cardAuthorBtn[s] && cardAuthorBtn[s]->getState() == STATE::CLICKED)
					{
						cardAuthorBtn[s]->resetState();
						if(cardBtn[s]) cardBtn[s]->resetState();
						int chosen = startIdx + s;
						const char * cid = currentResults[chosen].channelId[0] ? currentResults[chosen].channelId : currentResults[chosen].author;
						clearThumbnails();
						mainWindow->remove(&resultsWin);
						MenuChannel(cid, currentResults[chosen].author, currentResults[chosen].avatarUrl);
						stayInResults = false;
						break;
					}

					if(cardBtn[s] && cardBtn[s]->getState() == STATE::CLICKED)
					{
						cardBtn[s]->resetState();
						int chosen = startIdx + s;

						if(currentResults[chosen].isChannel)
						{
							clearThumbnails();
							mainWindow->remove(&resultsWin);
							MenuChannel(currentResults[chosen].channelId, currentResults[chosen].title, currentResults[chosen].avatarUrl);
							stayInResults = false;
							break;
						}
						else if(currentResults[chosen].isPlaylist)
						{
							clearThumbnails();
							mainWindow->remove(&resultsWin);
							const char *plId = currentResults[chosen].playlistId[0] ? currentResults[chosen].playlistId : currentResults[chosen].videoId;
							MenuPlaylist(plId, currentResults[chosen].title);
							stayInResults = false;
							break;
						}

						struct ResolveTask
						{
							char videoId[32];
							char streamUrl[8192];
							char error[128];
							YtResult * result;
							bool success = false;
							volatile bool done = false;
						};
						std::unique_ptr<ResolveTask> rTask(new ResolveTask());

						snprintf(rTask->videoId, sizeof(rTask->videoId), "%s", currentResults[chosen].videoId);
						rTask->streamUrl[0] = '\0';
						rTask->error[0] = '\0';
						rTask->result = &currentResults[chosen];
						rTask->done = false;

						Thread resolveThread;
						resolveThread.start([](void * arg) -> void * {
							ResolveTask * t = static_cast<ResolveTask *>(arg);
							t->success = ytResolveStream(t->videoId, t->streamUrl, sizeof(t->streamUrl), t->error, sizeof(t->error), t->result);
							t->done = true;
							return nullptr;
						}, rTask.get(), 64 * 1024, ThreadPriority::Normal);

						RunWithLoadingScreen("BrewTube", "Resolving video stream", rTask->done);
						resolveThread.join();

						if(rTask->success)
						{
							HaltDeviceCheckingThread();
							mainWindow->setState(STATE::DISABLED);
							PlayResult playRes = PlayFile(rTask->streamUrl, rTask->error, sizeof(rTask->error), currentResults[chosen].title, &currentResults[chosen]);
							mainWindow->setState(STATE::DEFAULT);
							ResumeDeviceCheckingThread();

							while(playRes == PLAY_NEXT_VIDEO)
							{
								YtResult nextVid;
								if(!GetNextVideo(&nextVid))
									break;

								snprintf(rTask->videoId, sizeof(rTask->videoId), "%s", nextVid.videoId);
								rTask->streamUrl[0] = '\0';
								rTask->error[0] = '\0';
								rTask->result = &nextVid;
								rTask->done = false;

								Thread nextThread;
								nextThread.start([](void * arg) -> void * {
									ResolveTask * t = static_cast<ResolveTask *>(arg);
									t->success = ytResolveStream(t->videoId, t->streamUrl, sizeof(t->streamUrl), t->error, sizeof(t->error), t->result);
									t->done = true;
									return nullptr;
								}, rTask.get(), 64 * 1024, ThreadPriority::Normal);

								RunWithLoadingScreen("BrewTube", "Resolving video stream", rTask->done);
								nextThread.join();

								if(rTask->success)
								{
									HaltDeviceCheckingThread();
									mainWindow->setState(STATE::DISABLED);
									playRes = PlayFile(rTask->streamUrl, rTask->error, sizeof(rTask->error), nextVid.title, &nextVid);
									mainWindow->setState(STATE::DEFAULT);
									ResumeDeviceCheckingThread();
								}
								else
								{
									WindowPrompt("Playback Failed", rTask->error[0] ? rTask->error : "Could not resolve stream URL", "OK", nullptr);
									break;
								}
							}

							while(true)
							{
								platform->getInput()->update();
								bool held = false;
								for(int c = 0; c < 4; c++)
								{
									if(controller[c]->getPadData().buttons_h & (INPUT_BTN_B | INPUT_BTN_1))
										held = true;
								}
								if(!held)
									break;
								usleep(10000);
							}
							platform->getInput()->update();

							for(int i = 0; i < visibleCount; i++)
							{
								if(cardBtn[i])
									cardBtn[i]->resetState();
							}

							if(playRes == PLAY_EXIT) return;
							if(playRes == PLAY_CHANNEL)
							{
								char nextChan[64], nextAuthor[128], nextAvatar[256];
								if(GetNextChannel(nextChan, sizeof(nextChan), nextAuthor, sizeof(nextAuthor), nextAvatar, sizeof(nextAvatar)))
								{
									clearThumbnails();
									mainWindow->remove(&resultsWin);
									MenuChannel(nextChan, nextAuthor, nextAvatar);
									stayInResults = false;
									break;
								}
							}
							if(playRes == PLAY_ERROR) WindowPrompt("Error", rTask->error, "OK", nullptr);
						}
						else
						{
							WindowPrompt("Playback Failed", rTask->error[0] ? rTask->error : "Could not resolve stream URL", "OK", nullptr);
						}
					}
				}

				if(newSearchBtn.getState() == STATE::CLICKED)
				{
					newSearchBtn.resetState();
					view = VIEW_SEARCH;
					stayInResults = false;
				}

				if(totalPages > 1 && resultPage > 0 && prevBtn.getState() == STATE::CLICKED)
				{
					prevBtn.resetState();
					resultPage--;
					stayInResults = false;
				}

				if(canGoNext && nextBtn.getState() == STATE::CLICKED)
				{
					nextBtn.resetState();
					loadMoreResults();
					stayInResults = false;
				}

				if(otherBtn.getState() == STATE::CLICKED)
				{
					otherBtn.resetState();
					int otherChoice = ShowOtherMenuPrompt();
					if(otherChoice == 1)
					{
						int res = MenuBrowseFiles();
						if(res == MENU_EXIT) return;
					}
					else if(otherChoice == 2)
					{
						char streamUrl[512];
						if(EnterUrlPrompt(streamUrl, sizeof(streamUrl)))
						{
							char error[128] = "";
							HaltDeviceCheckingThread();
							mainWindow->setState(STATE::DISABLED);
							PlayResult playRes = PlayFile(streamUrl, error, sizeof(error));
							mainWindow->setState(STATE::DEFAULT);
							ResumeDeviceCheckingThread();

							while(true)
							{
								platform->getInput()->update();
								bool held = false;
								for(int c = 0; c < 4; c++)
								{
									if(controller[c]->getPadData().buttons_h & (INPUT_BTN_B | INPUT_BTN_1))
										held = true;
								}
								if(!held)
									break;
								usleep(10000);
							}
							platform->getInput()->update();

							if(playRes == PLAY_EXIT) return;
							if(playRes == PLAY_ERROR) WindowPrompt("Error", error, "OK", nullptr);
						}
					}
					else if(otherChoice == 3)
					{
						mainWindow->remove(&resultsWin);
						MenuSubscriptions();
						stayInResults = false;
						break;
					}
					else if(otherChoice == 4)
					{
						mainWindow->remove(&resultsWin);
						MenuLocalPlaylists();
						stayInResults = false;
						break;
					}
				}

				if(exitBtn.getState() == STATE::CLICKED)
				{
					platform->triggerExit();
					return;
				}

				uint32_t pressed = 0;
				for(int i = 0; i < 4; i++)
					pressed |= controller[i]->getPadData().buttons_d;
				if(pressed & INPUT_BTN_B)
				{
					view = VIEW_SEARCH;
					stayInResults = false;
				}
				if((pressed & INPUT_BTN_LEFT) && resultPage > 0)
				{
					resultPage--;
					stayInResults = false;
				}
				if((pressed & INPUT_BTN_RIGHT) && canGoNext)
				{
					loadMoreResults();
					stayInResults = false;
				}
			}

			for(int s = 0; s < visibleCount; s++)
			{
				delete cardBtn[s];
				delete cardAuthorBtn[s];
				delete cardBg[s];
				delete cardBgOver[s];
				delete cardTitle[s];
				delete cardAuthor[s];
				delete cardMeta[s];
			}
		}
	}

	if(gSuggestTask)
	{
		gSuggestTask->stop = true;
		if(gSuggestTask->thread.isRunning())
			gSuggestTask->thread.join();
		delete gSuggestTask;
		gSuggestTask = nullptr;
	}

	clearThumbnails();
}

void MainMenu()
{
	ResumeDeviceCheckingThread();
	prefsLoad();

	pointer[0] = new GuiImageData(player1_point_png);
	pointer[1] = new GuiImageData(player2_point_png);
	pointer[2] = new GuiImageData(player3_point_png);
	pointer[3] = new GuiImageData(player4_point_png);

	mainWindow = new GuiWindow(platform->getVideo()->getScreenWidth(), platform->getVideo()->getScreenHeight());

	GuiImage * bgImg = new GuiImage(platform->getVideo()->getScreenWidth(), platform->getVideo()->getScreenHeight(), (PixelColor){18, 20, 28, 255});
	mainWindow->append(bgImg);

	bool netUp = false;
	while(!netUp)
	{
		struct NetInitTask
		{
			volatile bool done = false;
			int result = 0;
		} nTask;

		Thread nThread;
		nThread.start([](void * arg) -> void * {
			NetInitTask * t = static_cast<NetInitTask *>(arg);
			t->result = netInit();
			t->done = true;
			return nullptr;
		}, &nTask, 64 * 1024, ThreadPriority::Normal);

		if(!RunWithLoadingScreen("BrewTube", "Connecting to network...", nTask.done))
		{
			nThread.join();
			HaltDeviceCheckingThread();
			delete bgImg;
			delete mainWindow;
			mainWindow = nullptr;
			for(int i = 0; i < 4; i++) delete pointer[i];
			return;
		}
		nThread.join();

		netUp = nTask.result != 0;
		if(!netUp && WindowPrompt("No Network", "Could not connect to the network. Make sure your console is connected to the internet.", "Retry", "Exit") == 0)
		{
			HaltDeviceCheckingThread();
			delete bgImg;
			delete mainWindow;
			mainWindow = nullptr;
			for(int i = 0; i < 4; i++) delete pointer[i];
			return;
		}
	}

	ytStartUpdateCheck();

	MenuBrewTube();

	HaltDeviceCheckingThread();

	delete bgImg;
	delete mainWindow;
	mainWindow = nullptr;

	for(int i = 0; i < 4; i++)
		delete pointer[i];
}
