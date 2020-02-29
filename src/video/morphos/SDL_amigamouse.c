/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2014 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "../../SDL_internal.h"

#include "SDL_amigamouse.h"
#include "SDL_amigavideo.h"

#include "../../events/SDL_mouse_c.h"

#include <cybergraphx/cybergraphics.h>
#include <devices/input.h>
#include <intuition/pointerclass.h>
#include <proto/cybergraphics.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>


static SDL_Cursor *
AMIGA_CreateCursor(SDL_Surface * surface, int hot_x, int hot_y)
{
	SDL_AmigaCursor *cursor = SDL_malloc(sizeof(*cursor));
	D("[%s]\n", __FUNCTION__);

	if (cursor)
	{
		SDL_AmigaCursor *ac = SDL_malloc(sizeof(*ac));
		struct BitMap *bmp;

		cursor->Cursor.next = NULL;
		cursor->Cursor.driverdata = &cursor->Pointer;
		cursor->Pointer.offx = hot_x;
		cursor->Pointer.offy = hot_y;

		bmp = AllocBitMap(surface->w, surface->h, 32, BMF_MINPLANES | BMF_SPECIALFMT | SHIFT_PIXFMT(PIXFMT_ARGB32), NULL);

		if (bmp != NULL)
		{
			struct RastPort rp;

			cursor->Pointer.bmp = bmp;

			InitRastPort(&rp);
			rp.BitMap = bmp;

			if (SDL_LockSurface(surface) == 0)
			{
				WritePixelArray(surface->pixels, 0, 0, surface->pitch, &rp, 0, 0, surface->w, surface->h, RECTFMT_ARGB);
				SDL_UnlockSurface(surface);
			}
		}
		else
		{
			SDL_free(cursor);
			cursor = NULL;
		}
	}

	return &cursor->Cursor;
}

static SDL_Cursor *
AMIGA_CreateSystemCursor(SDL_SystemCursor id)
{
	SDL_Cursor *cursor = SDL_malloc(sizeof(*cursor));
	D("[%s]\n", __FUNCTION__);

	if (cursor)
	{
		size_t type = POINTERTYPE_NORMAL;

		cursor->next = NULL;

		switch (id)
		{
			default:
			case SDL_SYSTEM_CURSOR_ARROW:     type = POINTERTYPE_NORMAL; break;
			case SDL_SYSTEM_CURSOR_IBEAM:     type = POINTERTYPE_SELECTTEXT; break;
			case SDL_SYSTEM_CURSOR_WAIT:      type = POINTERTYPE_BUSY; break;
			case SDL_SYSTEM_CURSOR_CROSSHAIR: type = POINTERTYPE_AIMING; break;
			case SDL_SYSTEM_CURSOR_WAITARROW: type = POINTERTYPE_WORKING; break;
			case SDL_SYSTEM_CURSOR_SIZENWSE:  type = POINTERTYPE_VERTICALRESIZE; break;
			case SDL_SYSTEM_CURSOR_SIZENESW:  type = POINTERTYPE_VERTICALRESIZE; break;
			case SDL_SYSTEM_CURSOR_SIZEWE:    type = POINTERTYPE_HORIZONTALRESIZE; break;
			case SDL_SYSTEM_CURSOR_SIZENS:    type = POINTERTYPE_HORIZONTALRESIZE; break;
			case SDL_SYSTEM_CURSOR_SIZEALL:   type = POINTERTYPE_MOVE; break;
			case SDL_SYSTEM_CURSOR_NO:        type = POINTERTYPE_NOTAVAILABLE; break;
			case SDL_SYSTEM_CURSOR_HAND:      type = POINTERTYPE_SELECTLINK; break;
		}

		cursor->driverdata = (APTR)type;
		D("[%s] %ld to %ld\n", __FUNCTION__, id, type);
	}
	else
	{
		SDL_OutOfMemory();
	}

	return cursor;
}

static void
AMIGA_FreeCursor(SDL_Cursor *cursor)
{
	D("[%s] 0x%08lx\n", __FUNCTION__, cursor);

	if (!IS_SYSTEM_CURSOR(cursor))
	{
		FreeBitMap(((SDL_AmigaCursor *)cursor)->Pointer.bmp);
	}

	SDL_free(cursor);
}

static int
AMIGA_ShowCursor(SDL_Cursor * cursor)
{
	SDL_VideoDevice *video = SDL_GetVideoDevice();
	SDL_VideoData *data = (SDL_VideoData *)video->driverdata;
	D("[%s] 0x%08lx\n", __FUNCTION__, cursor);

	if (IS_SYSTEM_CURSOR(cursor))
	{
		size_t type = cursor ? (size_t)cursor->driverdata : POINTERTYPE_INVISIBLE;

		if (data->CurrentPointer != cursor)
		{
			SDL_WindowData *wd;
			size_t pointertags[] = { WA_PointerType, type, TAG_DONE };

			ForeachNode(&data->windowlist, wd)
			{
				if (wd->win)
				{
					SetAttrsA(wd->win, (struct TagItem *)&pointertags);
				}
			}
		}
	}
	else
	{
		SDL_AmigaCursor *ac = (SDL_AmigaCursor *)cursor;
		SDL_WindowData *wd;
		size_t pointertags[] = { POINTERA_BitMap, (size_t)ac->Pointer.bmp, POINTERA_XOffset, ac->Pointer.offx, POINTERA_YOffset, ac->Pointer.offy, TAG_DONE };

		ForeachNode(&data->windowlist, wd)
		{
			if (wd->win)
			{
				SetWindowPointerA(wd->win, (struct TagItem *)&pointertags);
			}
		}
	}

	data->CurrentPointer = cursor;

	return 0;
}

static void
AMIGA_WarpMouse(SDL_Window * window, int x, int y)
{
	SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
	struct Window *win;
	D("[%s]\n", __FUNCTION__);

	if ((win = data->win))
	{
		struct MsgPort port;
		struct IOStdReq req;

		port.mp_Flags = PA_IGNORE;
		NEWLIST(&port.mp_MsgList);

		req.io_Message.mn_ReplyPort = &port;
		req.io_Device = NULL;
		req.io_Unit = NULL;

		if (OpenDevice("input.device", 0, (struct IORequest *)&req, 0) == 0)
		{
			struct InputEvent ie;
			struct IEPointerPixel newpos;

			newpos.iepp_Screen = win->WScreen;
			newpos.iepp_Position.X = x + win->BorderLeft + win->LeftEdge;
			newpos.iepp_Position.Y = y + win->BorderTop + win->TopEdge;

			ie.ie_EventAddress = &newpos;
			ie.ie_NextEvent = NULL;
			ie.ie_Class = IECLASS_NEWPOINTERPOS;
			ie.ie_SubClass = IESUBCLASS_PIXEL;
			ie.ie_Code = IECODE_NOBUTTON;
			ie.ie_Qualifier = 0;

			req.io_Data = &ie;
			req.io_Length = sizeof(ie);
			req.io_Command = IND_WRITEEVENT;

			DoIO((struct IORequest *)&req);
			CloseDevice((struct IORequest *)&req);
		}
	}
}

static int
AMIGA_SetRelativeMouseMode(SDL_bool enabled)
{
	SDL_VideoDevice *video = SDL_GetVideoDevice();
	SDL_VideoData *data = (SDL_VideoData *)video->driverdata;
	SDL_WindowData *wd;
	size_t or_mask, and_mask;

	if (enabled)
	{
		or_mask = IDCMP_DELTAMOVE;
		and_mask = ~0;
	}
	else
	{
		or_mask = 0;
		and_mask = ~IDCMP_DELTAMOVE;
	}

	ForeachNode(&data->windowlist, wd)
	{
		if (wd->win)
		{
			ModifyIDCMP(wd->win, (wd->win->IDCMPFlags | or_mask) & and_mask);
		}
	}

	return 0;
}

void
AMIGA_InitMouse(_THIS)
{
	SDL_Mouse *mouse = SDL_GetMouse();

	mouse->CreateCursor = AMIGA_CreateCursor;
	mouse->CreateSystemCursor = AMIGA_CreateSystemCursor;
	mouse->ShowCursor = AMIGA_ShowCursor;
	mouse->FreeCursor = AMIGA_FreeCursor;
	mouse->WarpMouse = AMIGA_WarpMouse;
	mouse->SetRelativeMouseMode = AMIGA_SetRelativeMouseMode;

	//SDL_SetDefaultCursor(WIN_CreateDefaultCursor());
	//SDL_SetDoubleClickTime(GetDoubleClickTime());
}

void
AMIGA_QuitMouse(_THIS)
{
	SDL_Mouse *mouse = SDL_GetMouse();
	D("[%s]\n", __FUNCTION__);

	if ( mouse->def_cursor ) {
		SDL_free(mouse->def_cursor);
		mouse->def_cursor = NULL;
		mouse->cur_cursor = NULL;
	}
}

/* vi: set ts=4 sw=4 expandtab: */
