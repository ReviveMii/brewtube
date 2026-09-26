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
#include <vector>
#include <memory>

#include "libgui/Gui.h"
#include "drivers/Platform.h"
#include "menu.h"
#include "player.h"
#include "filelist.h"
#include "filebrowser.h"
#include "youtube.h"

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

static bool EnterUrlPrompt(char * url, int urlSize, const char * promptTitle = "Enter Media URL")
{
	static const char * const kbRow0 = "1234567890";
	static const char * const kbRow1 = "qwertyuiop";
	static const char * const kbRow2 = "asdfghjkl";
	static const char * const kbRow3 = "zxcvbnm./:-_";
	const int MAX_KEYS = 50;
	const int KEY_W = 44, KEY_H = 38, KEY_GAP = 5;
	const PixelColor white = {255, 255, 255, 255};
	const PixelColor btnText = {25, 28, 38, 255};

	url[0] = '\0';
	int len = 0;
	bool accepted = false;
	bool done = false;

	GuiWindow kbWindow(580, 390);
	kbWindow.setAlignment(ALIGN_H::CENTRE, ALIGN_V::MIDDLE);
	kbWindow.setPosition(0, -10);

	GuiImage kbBg(580, 390, (PixelColor){22, 25, 36, 250});
	GuiImage border(580, 3, (PixelColor){0, 120, 215, 255});
	border.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);

	GuiText titleTxt(promptTitle, 24, white);
	titleTxt.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	titleTxt.setPosition(0, 18);

	GuiImage barBg(520, 38, (PixelColor){32, 36, 48, 255});
	barBg.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	barBg.setPosition(0, 52);

	GuiText urlTxt("", 18, white);
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

	GuiImage spaceImg(&btnOutline);
	spaceImg.setSize(180, KEY_H);
	GuiImage spaceImgOver(&btnOutlineOver);
	spaceImgOver.setSize(180, KEY_H);
	GuiText spaceTxt("Space", 18, btnText);
	GuiButton spaceBtn(180, KEY_H);
	spaceBtn.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	spaceBtn.setPosition(-130, barY);
	spaceBtn.setImage(&spaceImg);
	spaceBtn.setImageOver(&spaceImgOver);
	spaceBtn.setLabel(&spaceTxt);
	spaceBtn.setSoundOver(&btnSoundOver);
	spaceBtn.setTrigger(&trigA);
	spaceBtn.setEffectGrow();

	GuiImage backImg(&btnOutline);
	backImg.setSize(80, KEY_H);
	GuiImage backImgOver(&btnOutlineOver);
	backImgOver.setSize(80, KEY_H);
	GuiText backTxt("Del", 18, btnText);
	GuiButton backBtn(80, KEY_H);
	backBtn.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	backBtn.setPosition(15, barY);
	backBtn.setImage(&backImg);
	backBtn.setImageOver(&backImgOver);
	backBtn.setLabel(&backTxt);
	backBtn.setSoundOver(&btnSoundOver);
	backBtn.setTrigger(&trigA);
	backBtn.setEffectGrow();

	GuiImage clearImg(&btnOutline);
	clearImg.setSize(80, KEY_H);
	GuiImage clearImgOver(&btnOutlineOver);
	clearImgOver.setSize(80, KEY_H);
	GuiText clearTxt("Clear", 18, btnText);
	GuiButton clearBtn(80, KEY_H);
	clearBtn.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	clearBtn.setPosition(105, barY);
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
	GuiText okTxt("Play", 20, btnText);
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

	kbWindow.append(&spaceBtn);
	kbWindow.append(&backBtn);
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
				if(len < urlSize - 1)
				{
					url[len++] = keyChar[i];
					url[len] = '\0';
					urlTxt.setText(url);
				}
			}
		}

		if(spaceBtn.getState() == STATE::CLICKED)
		{
			spaceBtn.resetState();
			if(len < urlSize - 1)
			{
				url[len++] = ' ';
				url[len] = '\0';
				urlTxt.setText(url);
			}
		}

		if(backBtn.getState() == STATE::CLICKED)
		{
			backBtn.resetState();
			if(len > 0)
			{
				url[--len] = '\0';
				urlTxt.setText(url);
			}
		}

		if(clearBtn.getState() == STATE::CLICKED)
		{
			clearBtn.resetState();
			len = 0;
			url[0] = '\0';
			urlTxt.setText(url);
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
				url[--len] = '\0';
				urlTxt.setText(url);
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
		url[0] = '\0';

	return accepted;
}

static int ShowOtherMenuPrompt()
{
	int choice = -1;

	GuiWindow otherWin(460, 305);
	otherWin.setAlignment(ALIGN_H::CENTRE, ALIGN_V::MIDDLE);
	otherWin.setPosition(0, -10);

	GuiSound btnSoundOver(button_over_pcm, button_over_pcm_size, SOUND::PCM);
	GuiImageData btnOutline(button_png);
	GuiImageData btnOutlineOver(button_over_png);
	GuiTrigger trigA;
	trigA.setPrimaryTrigger();

	GuiImage bg(460, 340, (PixelColor){22, 25, 36, 250});
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
	filesBtn.setPosition(0, 48);
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
	urlBtn.setPosition(0, 94);
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
	GuiText chanTxt("Open Channel (@handle / ID)", 18, btnText);
	GuiButton chanBtn(290, 40);
	chanBtn.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	chanBtn.setPosition(0, 140);
	chanBtn.setImage(&chanImg);
	chanImgOver.setSize(290, 40);
	chanBtn.setImageOver(&chanImgOver);
	chanBtn.setLabel(&chanTxt);
	chanBtn.setSoundOver(&btnSoundOver);
	chanBtn.setTrigger(&trigA);
	chanBtn.setEffectGrow();

	GuiImage clientImg(&btnOutline);
	clientImg.setSize(290, 40);
	GuiImage clientImgOver(&btnOutlineOver);
	clientImgOver.setSize(290, 40);
	char clientStr[48];
	snprintf(clientStr, sizeof(clientStr), "YTVideoClient: %s", ytGetClient() == YT_CLIENT_VISIONOS ? "VISIONOS" : "ANDROID");
	GuiText clientTxt(clientStr, 18, btnText);
	GuiButton clientBtn(290, 40);
	clientBtn.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	clientBtn.setPosition(0, 186);
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
	cancelBtn.setPosition(0, 246);
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
		void * tex = t->results[i].isChannel
			? ytFetchImage(t->results[i].avatarUrl, 88, 88, &w, &h)
			: ytFetchThumbnail(t->results[i].videoId, 144, 81, &w, &h);
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
					if(items[idx].isPlayable)
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
			searchBarBg.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
			searchBarBg.setPosition(0, 62);

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

			searchWin.append(&searchBarBg);
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

			addKeyRow(kbRow0, 118);
			addKeyRow(kbRow1, 168);
			addKeyRow(kbRow2, 218);
			addKeyRow(kbRow3, 268);

			int barStartX = (sw - (10 * KEY_W + 9 * KEY_GAP)) / 2;
			int barY = 318;

			GuiImage spaceImg(&btnOutline);
			spaceImg.setSize(220, KEY_H);
			GuiImage spaceImgOver(&btnOutlineOver);
			spaceImgOver.setSize(220, KEY_H);
			GuiText spaceTxt("Space", 19, btnText);
			GuiButton spaceBtn(220, KEY_H);
			spaceBtn.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
			spaceBtn.setPosition(barStartX, barY);
			spaceBtn.setImage(&spaceImg);
			spaceBtn.setImageOver(&spaceImgOver);
			spaceBtn.setLabel(&spaceTxt);
			spaceBtn.setSoundOver(&btnSoundOver);
			spaceBtn.setTrigger(&trigA);
			spaceBtn.setEffectGrow();

			GuiImage delImg(&btnOutline);
			delImg.setSize(75, KEY_H);
			GuiImage delImgOver(&btnOutlineOver);
			delImgOver.setSize(75, KEY_H);
			GuiText delTxt("Del", 19, btnText);
			GuiButton delBtn(75, KEY_H);
			delBtn.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
			delBtn.setPosition(barStartX + 228, barY);
			delBtn.setImage(&delImg);
			delBtn.setImageOver(&delImgOver);
			delBtn.setLabel(&delTxt);
			delBtn.setSoundOver(&btnSoundOver);
			delBtn.setTrigger(&trigA);
			delBtn.setEffectGrow();

			GuiImage clearImg(&btnOutline);
			clearImg.setSize(75, KEY_H);
			GuiImage clearImgOver(&btnOutlineOver);
			clearImgOver.setSize(75, KEY_H);
			GuiText clearTxt("Clear", 19, btnText);
			GuiButton clearBtn(75, KEY_H);
			clearBtn.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
			clearBtn.setPosition(barStartX + 311, barY);
			clearBtn.setImage(&clearImg);
			clearBtn.setImageOver(&clearImgOver);
			clearBtn.setLabel(&clearTxt);
			clearBtn.setSoundOver(&btnSoundOver);
			clearBtn.setTrigger(&trigA);
			clearBtn.setEffectGrow();

			GuiImage searchImg(&btnOutline);
			searchImg.setSize(140, KEY_H);
			GuiImage searchImgOver(&btnOutlineOver);
			searchImgOver.setSize(140, KEY_H);
			GuiText searchTxt("Search", 20, btnText);
			GuiButton searchBtn(140, KEY_H);
			searchBtn.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
			searchBtn.setPosition(barStartX + 394, barY);
			searchBtn.setImage(&searchImg);
			searchBtn.setImageOver(&searchImgOver);
			searchBtn.setLabel(&searchTxt);
			searchBtn.setSoundOver(&btnSoundOver);
			searchBtn.setTrigger(&trigA);
			searchBtn.setEffectGrow();

			searchWin.append(&spaceBtn);
			searchWin.append(&delBtn);
			searchWin.append(&clearBtn);
			searchWin.append(&searchBtn);

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
				}

				uint32_t pressed = 0;
				for(int i = 0; i < 4; i++)
					pressed |= controller[i]->getPadData().buttons_d;

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
						char chanInput[128];
						if(EnterUrlPrompt(chanInput, sizeof(chanInput), "Enter Channel (@handle / ID)"))
						{
							mainWindow->remove(&searchWin);
							MenuChannel(chanInput);
							mainWindow->appendWithAutoRemove(&searchWin);
						}
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

				if(searchBtn.getState() == STATE::CLICKED || (pressed & INPUT_BTN_PLUS))
				{
					searchBtn.resetState();
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

			for(int i = 0; i < keyCount; i++)
			{
				delete keyBtn[i];
				delete keyTxt[i];
				delete keyImg[i];
				delete keyImgOver[i];
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

					if(currentResults[idx].publishedText[0] != '\0' && currentResults[idx].viewCountText[0] != '\0')
					{
						snprintf(cardMetaStr[s], sizeof(cardMetaStr[s]), "%s   •   %s   •   %s", currentResults[idx].lengthText, currentResults[idx].viewCountText, currentResults[idx].publishedText);
					}
					else if(currentResults[idx].publishedText[0] != '\0')
					{
						snprintf(cardMetaStr[s], sizeof(cardMetaStr[s]), "%s   •   %s", currentResults[idx].lengthText, currentResults[idx].publishedText);
					}
					else
					{
						snprintf(cardMetaStr[s], sizeof(cardMetaStr[s]), "%s   •   %s", currentResults[idx].lengthText, currentResults[idx].viewCountText);
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
						char chanInput[128];
						if(EnterUrlPrompt(chanInput, sizeof(chanInput), "Enter Channel (@handle / ID)"))
						{
							mainWindow->remove(&resultsWin);
							MenuChannel(chanInput);
							stayInResults = false;
						}
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

	clearThumbnails();
}

void MainMenu()
{
	ResumeDeviceCheckingThread();
	ytStartUpdateCheck();

	pointer[0] = new GuiImageData(player1_point_png);
	pointer[1] = new GuiImageData(player2_point_png);
	pointer[2] = new GuiImageData(player3_point_png);
	pointer[3] = new GuiImageData(player4_point_png);

	mainWindow = new GuiWindow(platform->getVideo()->getScreenWidth(), platform->getVideo()->getScreenHeight());

	GuiImage * bgImg = new GuiImage(platform->getVideo()->getScreenWidth(), platform->getVideo()->getScreenHeight(), (PixelColor){18, 20, 28, 255});
	mainWindow->append(bgImg);

	MenuBrewTube();

	HaltDeviceCheckingThread();

	delete bgImg;
	delete mainWindow;
	mainWindow = nullptr;

	for(int i = 0; i < 4; i++)
		delete pointer[i];
}
