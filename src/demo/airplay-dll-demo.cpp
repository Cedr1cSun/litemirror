// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
//
// Minimal receiver window: start the server, show the mirrored screen,
// play the audio. [s] starts, [q] stops, closing the window quits.
#include <windows.h>
#include <stdio.h>
#include "CSDLPlayer.h"

int main()
{
	SetConsoleOutputCP(CP_UTF8);   // device names are UTF-8
	printf("LiteMirror demo\n [s] start server\n [q] stop server\n\n");

	CSDLPlayer player;
	if (!player.init()) {
		return 1;
	}
	player.loopEvents();
	return 0;
}
