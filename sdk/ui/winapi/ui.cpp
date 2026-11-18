#include "ui.h"
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")
#include <Windowsx.h>
// the checkbox tells screen readers what it is (Dynamic Annotation, oleacc)
#include <initguid.h>
#include <oleacc.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

namespace AVS
{
	// BUTTONS
	ButtonSettings ButtonSettings::Create(const Buttons& type)
	{
		ButtonSettings settings;

		switch (type)
		{
			case Buttons::Primary:
			{
				settings.Background = Color::MakeRGBA(55, 119, 227);
				settings.Border = Color::MakeRGBA(43, 93, 179);

				settings.BackgroundHover = Color::MakeRGBA(68, 132, 240);
				settings.BorderHover = Color::MakeRGBA(22, 61, 128);

				settings.BackgroundPressed = Color::MakeRGBA(255, 255, 255);
				settings.BorderPressed = Color::MakeRGBA(206, 215, 230);

				settings.TextColor = Color::MakeRGBA(255, 255, 255);
				settings.TextColorPressed = Color::MakeRGBA(0, 0, 0);

				settings.FocusColor = Color::MakeRGBA(22, 61, 128);
				break;
			}
			case Buttons::ToggleGroupEnable:
			{
				settings.Background = Color::MakeRGBA(55, 119, 227);
				settings.Border = Color::MakeRGBA(43, 93, 179);

				settings.BackgroundHover = Color::MakeRGBA(68, 132, 240);
				settings.BorderHover = Color::MakeRGBA(52, 105, 194);

				settings.BackgroundPressed = Color::MakeRGBA(43, 104, 209);
				settings.BorderPressed = Color::MakeRGBA(31, 77, 156);

				settings.TextColor = Color::MakeRGBA(255, 255, 255);
				settings.TextColorPressed = Color::MakeRGBA(255, 255, 255);

				settings.FocusColor = Color::MakeRGBA(22, 61, 128);
				break;
			}
			case Buttons::ToggleGroupDisable:
			{
				settings.Background = Color::MakeRGBA(218, 226, 242);
				settings.Border = Color::MakeRGBA(190, 201, 224);

				settings.BackgroundHover = Color::MakeRGBA(228, 234, 247);
				settings.BorderHover = Color::MakeRGBA(160, 177, 210);

				settings.BackgroundPressed = Color::MakeRGBA(200, 214, 240);
				settings.BorderPressed = Color::MakeRGBA(120, 145, 194);

				settings.TextColor = Color::MakeRGBA(0, 0, 0);
				settings.TextColorPressed = Color::MakeRGBA(0, 0, 0);

				settings.FocusColor = Color::MakeRGBA(57, 114, 227);
				break;
			}
			case Buttons::Default:
			default:
			{
				settings.Background = Color::MakeRGBA(218, 226, 242);
				settings.Border = Color::MakeRGBA(184, 196, 217);

				settings.BackgroundHover = Color::MakeRGBA(218, 226, 242);
				settings.BorderHover = Color::MakeRGBA(119, 137, 171);

				settings.BackgroundPressed = Color::MakeRGBA(255, 255, 255);
				settings.BorderPressed = Color::MakeRGBA(206, 215, 230);

				settings.TextColor = Color::MakeRGBA(0, 0, 0);
				settings.TextColorPressed = Color::MakeRGBA(0, 0, 0);

				settings.FocusColor = Color::MakeRGBA(57, 114, 227);
				break;
			}
		}

		return settings;
	}

	class ButtonInfo
	{
	public:
		ButtonSettings Settings;

		bool  IsHovered;
		HFONT hFont;

	public:
		ButtonInfo(const ButtonSettings& settings)
		{
			Settings = settings;
			IsHovered = false;
			
			int fontH = Settings.FontSize;
			if (fontH > 0)
				fontH = -MulDiv(fontH, 96, 72);
			hFont = CreateFontW(
				fontH,
				0, 0, 0,
				Settings.FontBold ? FW_BOLD : FW_NORMAL,
				Settings.FontItalic ? TRUE : FALSE, FALSE, FALSE,
				DEFAULT_CHARSET,
				OUT_DEFAULT_PRECIS,
				CLIP_DEFAULT_PRECIS,
				DEFAULT_QUALITY,
				DEFAULT_PITCH | FF_SWISS,
				Settings.FontName.c_str()
			);
		}

		~ButtonInfo()
		{
			DeleteObject(hFont);
		}
	};

	LRESULT CALLBACK _ButtonProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
	{
		ButtonInfo* info = (ButtonInfo*)dwRefData;

		switch (msg)
		{
		case WM_GETDLGCODE:
		{
			if (lParam)
			{
				MSG* pMsg = (MSG*)lParam;
				if (pMsg->message == WM_KEYDOWN && 
					(pMsg->wParam == VK_RETURN || pMsg->wParam == VK_SPACE))
				{
					return DLGC_WANTALLKEYS;
				}
			}
			return 0;
		}
		case WM_KEYDOWN:
		{
			if (wParam == VK_RETURN || wParam == VK_SPACE)
			{
				SendMessage(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), BN_CLICKED), (LPARAM)hwnd);
				return 0;
			}
			break;
		}
		case WM_MOUSEMOVE:
		{
			if (!info->IsHovered)
			{
				info->IsHovered = true;
				InvalidateRect(hwnd, NULL, FALSE);

				TRACKMOUSEEVENT tme = { sizeof(tme) };
				tme.dwFlags = TME_LEAVE;
				tme.hwndTrack = hwnd;
				TrackMouseEvent(&tme);
			}
			break;
		}
		case WM_MOUSELEAVE:
		{
			info->IsHovered = false;
			InvalidateRect(hwnd, NULL, FALSE);
			break;
		}
		case WM_SETFOCUS:
		case WM_KILLFOCUS:
		{
			InvalidateRect(hwnd, NULL, FALSE);
			break;
		}
		case WM_ENABLE:
		{
			// a disabled window gets no WM_MOUSELEAVE: drop the hover it had
			info->IsHovered = false;
			InvalidateRect(hwnd, NULL, FALSE);
			break;
		}
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hwnd, &ps);

			RECT rc;
			GetClientRect(hwnd, &rc);

			LRESULT state = SendMessage(hwnd, BM_GETSTATE, 0, 0);
			bool isPressed = (state & BST_PUSHED) != 0;

			// Disabled: the button's colours halfway to the window's background, the
			// text halfway to the button's own - as a disabled combo box shows.
			const bool isEnabled = IsWindowEnabled(hwnd) != FALSE;
			const Color window = Color::GetDefaultWindowBackground();
			const auto fade = [](const Color& c, const Color& to) {
				return Color::MakeRGBA(static_cast<unsigned char>((c.R + to.R) / 2),
					static_cast<unsigned char>((c.G + to.G) / 2),
					static_cast<unsigned char>((c.B + to.B) / 2), c.A);
			};

			Color backColor = isPressed ? info->Settings.BackgroundPressed : (info->IsHovered ? info->Settings.BackgroundHover : info->Settings.Background);
			if (!isEnabled)
				backColor = fade(info->Settings.Background, window);
			COLORREF bgColor = RGB(backColor.R, backColor.G, backColor.B);

			HBRUSH hBrush = CreateSolidBrush(bgColor);
			FillRect(hdc, &rc, hBrush);
			DeleteObject(hBrush);

			Color borderColor = isPressed ? info->Settings.BorderPressed : (info->IsHovered ? info->Settings.BorderHover : info->Settings.Border);
			if (!isEnabled)
				borderColor = fade(info->Settings.Border, window);

			bool isFocus = (GetFocus() == hwnd);
			int borderW = 1;
			if (isFocus && !isPressed && info->Settings.FocusColor.A != 0)
			{
				borderColor = info->Settings.FocusColor;
				borderW = 2;
			}

			if (borderColor.A != 0)
			{
				RECT borderRect = rc;
				int ceilOffset = borderW >> 1;
				int rbOffset = (borderW + 1) & 0x01;

				borderRect.left += ceilOffset;
				borderRect.top += ceilOffset;
				borderRect.right -= (ceilOffset - rbOffset);
				borderRect.bottom -= (ceilOffset - rbOffset);

				HPEN hPen = CreatePen(PS_SOLID, borderW, RGB(borderColor.R, borderColor.G, borderColor.B));
				HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
				HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
				Rectangle(hdc, borderRect.left, borderRect.top, borderRect.right, borderRect.bottom);
				SelectObject(hdc, hOldPen);
				SelectObject(hdc, hOldBrush);
				DeleteObject(hPen);
			}

			wchar_t text[256];
			GetWindowText(hwnd, text, 256);

			SetBkMode(hdc, TRANSPARENT);
			Color textColor = isPressed ? info->Settings.TextColorPressed : info->Settings.TextColor;
			if (!isEnabled)
				textColor = fade(info->Settings.TextColor, backColor);
			SetTextColor(hdc, RGB(textColor.R, textColor.G, textColor.B));

			HFONT hFont = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
			HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

			if (isPressed)
				rc.bottom += info->Settings.TextPressedOffset;
			DrawText(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

			SelectObject(hdc, hOldFont);
			EndPaint(hwnd, &ps);
			return 0;
		}
		case WM_NCDESTROY:
		{
			delete info;
			RemoveWindowSubclass(hwnd, _ButtonProc, uIdSubclass);
			break;
		}
		}
		return DefSubclassProc(hwnd, msg, wParam, lParam);
	}

	HWND CreateButton(HWND hParent, HMENU hMenu, HINSTANCE hInstance,
		LPCWSTR text,
		const int& x, const int& y, const int& w, const int& h,
		const ButtonSettings& settings)
	{
		HWND hwnd = CreateWindow(L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW | WS_TABSTOP,
			x, y, w, h, hParent, hMenu, hInstance, NULL);

		ButtonInfo* info = new ButtonInfo(settings);

		SendMessage(hwnd, WM_SETFONT, (WPARAM)info->hFont, TRUE);
		SetWindowSubclass(hwnd, _ButtonProc, 0, (DWORD_PTR)info);
		return hwnd;
	}

	void Button_SetSettings(HWND hwnd, const ButtonSettings& settings, const std::wstring& text)
	{
		ButtonInfo* info = nullptr;
		GetWindowSubclass(hwnd, _ButtonProc, 0, (DWORD_PTR*)&info);
		if (info)
		{
			if (info->hFont)
				DeleteObject(info->hFont);

			info->Settings = settings;

			int fontH = info->Settings.FontSize;
			if (fontH > 0)
				fontH = -MulDiv(fontH, 96, 72);
			info->hFont = CreateFontW(
				fontH,
				0, 0, 0,
				info->Settings.FontBold ? FW_BOLD : FW_NORMAL,
				info->Settings.FontItalic ? TRUE : FALSE, FALSE, FALSE,
				DEFAULT_CHARSET,
				OUT_DEFAULT_PRECIS,
				CLIP_DEFAULT_PRECIS,
				DEFAULT_QUALITY,
				DEFAULT_PITCH | FF_SWISS,
				info->Settings.FontName.c_str()
			);

			SendMessage(hwnd, WM_SETFONT, (WPARAM)info->hFont, TRUE);

			if (!text.empty())
				SetWindowText(hwnd, text.c_str());
			
			InvalidateRect(hwnd, NULL, TRUE);
		}
	}

	TextEditSettings TextEditSettings::Create()
	{
		TextEditSettings settings;
		settings.Border = Color::MakeRGBA(206, 215, 230);
		settings.BorderHover = Color::MakeRGBA(57, 114, 227);
		settings.BorderFocus = Color::MakeRGBA(57, 114, 227);
		settings.BorderWidth = 1;
		settings.BorderFocusWidth = 2;
		settings.BorderFocusWidth = 2;
		return settings;
	}

	class TextEditInfo
	{
	public:
		TextEditSettings Settings;
		bool IsHovered;
		bool IsEmpty = true;   // for the placeholder: repaint when this changes
		HFONT hFont;

		TextEditInfo(const TextEditSettings& settings)
		{
			Settings = settings;
			IsHovered = false;

			int fontH = Settings.FontSize;
			if (fontH > 0)
				fontH = -MulDiv(fontH, 96, 72);
			hFont = CreateFontW(
				fontH,
				0, 0, 0,
				Settings.FontBold ? FW_BOLD : FW_NORMAL,
				Settings.FontItalic ? TRUE : FALSE, FALSE, FALSE,
				DEFAULT_CHARSET,
				OUT_DEFAULT_PRECIS,
				CLIP_DEFAULT_PRECIS,
				DEFAULT_QUALITY,
				DEFAULT_PITCH | FF_SWISS,
				Settings.FontName.c_str()
			);
		}

		~TextEditInfo()
		{
			DeleteObject(hFont);
		}
	};

	LRESULT CALLBACK _EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

	// The placeholder in the edit's own font and the disabled-label grey, where its text would start.
	// Only over the part the edit has just repainted, and over its background first: an empty
	// single-line edit does not erase what it repaints, and text drawn over itself turns bold.
	static void _DrawEditPlaceholder(HWND hwnd, const TextEditSettings& settings, HFONT font, HRGN update)
	{
		RECT rc;
		SendMessage(hwnd, EM_GETRECT, 0, (LPARAM)&rc);
		const BOOL caretHidden = HideCaret(hwnd);
		HDC hdc = GetDC(hwnd);
		SelectClipRgn(hdc, update);
		// the brush the edit itself paints with
		const bool editable = IsWindowEnabled(hwnd) &&
			!(GetWindowLongPtrW(hwnd, GWL_STYLE) & ES_READONLY);
		HBRUSH background = (HBRUSH)SendMessage(GetParent(hwnd),
			editable ? WM_CTLCOLOREDIT : WM_CTLCOLORSTATIC, (WPARAM)hdc, (LPARAM)hwnd);
		if (!background)
			background = GetSysColorBrush(editable ? COLOR_WINDOW : COLOR_3DFACE);
		FillRgn(hdc, update, background);
		HFONT oldFont = (HFONT)SelectObject(hdc, font);
		SetBkMode(hdc, TRANSPARENT);
		SetTextColor(hdc, RGB(130, 130, 130));
		UINT format = DT_NOPREFIX | DT_EDITCONTROL | (settings.IsMultiline ? DT_WORDBREAK : DT_SINGLELINE);
		if (settings.TextAlign & ES_RIGHT)
			format |= DT_RIGHT;
		else if (settings.TextAlign & ES_CENTER)
			format |= DT_CENTER;
		DrawTextW(hdc, settings.Placeholder.c_str(), -1, &rc, format);
		SelectObject(hdc, oldFont);
		ReleaseDC(hwnd, hdc);
		if (caretHidden)
			ShowCaret(hwnd);
	}

	static LRESULT _EditSubclassImpl(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
	{
		TextEditInfo* info = (TextEditInfo*)dwRefData;

		switch (msg)
		{
		case WM_MOUSEMOVE:
		{
			if (!info->IsHovered)
			{
				info->IsHovered = true;
				RedrawWindow(hwnd, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);

				TRACKMOUSEEVENT tme = { sizeof(tme) };
				tme.dwFlags = TME_LEAVE;
				tme.hwndTrack = hwnd;
				TrackMouseEvent(&tme);
			}
			break;
		}
		case WM_MOUSELEAVE:
		{
			info->IsHovered = false;
			RedrawWindow(hwnd, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
			break;
		}
		case WM_NCCALCSIZE:
		{
			LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
			// A single-line EDIT draws its text at the top of the client area:
			// shrink that to one text line in the middle, the rest becomes frame.
			if (wParam && info->Settings.IsVCenter && !info->Settings.IsMultiline)
			{
				RECT& client = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam)->rgrc[0];
				HDC hdc = GetDC(hwnd);
				HFONT hOldFont = (HFONT)SelectObject(hdc, info->hFont);
				TEXTMETRICW tm = {};
				GetTextMetricsW(hdc, &tm);
				SelectObject(hdc, hOldFont);
				ReleaseDC(hwnd, hdc);
				const int line = tm.tmHeight + 2;
				const int available = client.bottom - client.top;
				if (available > line)
				{
					client.top += (available - line) / 2;
					client.bottom = client.top + line;
				}
			}
			return result;
		}
		case WM_NCHITTEST:
		{
			// clicks above and below the centred line still go to the edit
			if (info->Settings.IsVCenter && !info->Settings.IsMultiline)
			{
				RECT rc;
				GetWindowRect(hwnd, &rc);
				POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				if (PtInRect(&rc, pt))
					return HTCLIENT;
			}
			break;
		}
		case WM_NCPAINT:
		{
			LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);

			HDC hdc = GetWindowDC(hwnd);

			RECT rc;
			GetWindowRect(hwnd, &rc);
			OffsetRect(&rc, -rc.left, -rc.top);

			if (info->Settings.IsVCenter && !info->Settings.IsMultiline)
			{
				// the frame above and below the text line, in the field's own colour
				RECT client;
				GetClientRect(hwnd, &client);
				MapWindowPoints(hwnd, NULL, reinterpret_cast<POINT*>(&client), 2);
				RECT window;
				GetWindowRect(hwnd, &window);
				OffsetRect(&client, -window.left, -window.top);
				RECT inner = rc;
				InflateRect(&inner, -GetSystemMetrics(SM_CXEDGE), -GetSystemMetrics(SM_CYEDGE));
				const bool editable = IsWindowEnabled(hwnd) &&
					!(GetWindowLongPtrW(hwnd, GWL_STYLE) & ES_READONLY);
				// the brush the edit paints its text line with: the parent's, if it sets one
				HBRUSH field = (HBRUSH)SendMessage(GetParent(hwnd),
					editable ? WM_CTLCOLOREDIT : WM_CTLCOLORSTATIC, (WPARAM)hdc, (LPARAM)hwnd);
				if (!field)
					field = GetSysColorBrush(editable ? COLOR_WINDOW : COLOR_3DFACE);
				int saved = SaveDC(hdc);
				ExcludeClipRect(hdc, client.left, client.top, client.right, client.bottom);
				FillRect(hdc, &inner, field);
				RestoreDC(hdc, saved);
			}

			bool hasFocus = (GetFocus() == hwnd);			

			Color* borderColor;
			int borderW;

			if (hasFocus)
			{
				borderColor = &info->Settings.BorderFocus;
				borderW = info->Settings.BorderFocusWidth;
			}
			else if (info->IsHovered)
			{
				borderColor = &info->Settings.BorderHover;
				borderW = info->Settings.BorderHoverWidth;
			}
			else
			{
				borderColor = &info->Settings.Border;
				borderW = info->Settings.BorderWidth;
			}

			if (borderColor->A != 0)
			{
				HPEN hPen = CreatePen(PS_SOLID, borderW, RGB(borderColor->R, borderColor->G, borderColor->B));
				HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
				HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));

				RECT borderRect = rc;
				int ceilOffset = borderW >> 1;
				int rbOffset = (borderW + 1) & 0x01;

				borderRect.left += ceilOffset;
				borderRect.top += ceilOffset;
				borderRect.right -= (ceilOffset - rbOffset);
				borderRect.bottom -= (ceilOffset - rbOffset);

				Rectangle(hdc, borderRect.left, borderRect.top, borderRect.right, borderRect.bottom);

				SelectObject(hdc, hOldPen);
				SelectObject(hdc, hOldBrush);
				DeleteObject(hPen);
			}

			ReleaseDC(hwnd, hdc);
			return result;
		}
		case WM_SETFOCUS:
		case WM_KILLFOCUS:
		{
			LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
			RedrawWindow(hwnd, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
			return result;
		}
		case WM_KEYDOWN:
		{
			// For tab navigation (input tab with ctrl!)
			if (wParam == VK_TAB && !(GetKeyState(VK_CONTROL) & 0x8000))
			{
				HWND hParent = GetParent(hwnd);
				HWND hNext = GetNextDlgTabItem(hParent, hwnd, GetKeyState(VK_SHIFT) < 0);
				SetFocus(hNext);
				return 0;
			}
			break;
		}
		case WM_GETDLGCODE:
		{
			if (lParam)
			{
				MSG* pMsg = (MSG*)lParam;
				if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_TAB)
				{
					if (GetKeyState(VK_CONTROL) & 0x8000)
						return DLGC_WANTALLKEYS | DLGC_WANTTAB;
					return 0;
				}
			}
			break;
		}
		case WM_NCDESTROY:
		{
			delete info;
			RemoveWindowSubclass(hwnd, _EditSubclassProc, uIdSubclass);
			break;
		}
		}

		return DefSubclassProc(hwnd, msg, wParam, lParam);
	}

	LRESULT CALLBACK _EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
	{
		TextEditInfo* info = (TextEditInfo*)dwRefData;
		if (msg == WM_NCDESTROY || info->Settings.Placeholder.empty())
			return _EditSubclassImpl(hwnd, msg, wParam, lParam, uIdSubclass, dwRefData);

		if (msg == WM_PAINT)
		{
			// after the edit has painted itself: the placeholder over an empty field, in what
			// was to be repainted (BeginPaint in the edit validates it, so it is taken first)
			HRGN update = CreateRectRgn(0, 0, 0, 0);
			const int region = GetUpdateRgn(hwnd, update, FALSE);
			LRESULT result = _EditSubclassImpl(hwnd, msg, wParam, lParam, uIdSubclass, dwRefData);
			if (region != NULLREGION && region != ERROR && GetWindowTextLengthW(hwnd) == 0)
				_DrawEditPlaceholder(hwnd, info->Settings, info->hFont, update);
			DeleteObject(update);
			return result;
		}

		LRESULT result = _EditSubclassImpl(hwnd, msg, wParam, lParam, uIdSubclass, dwRefData);
		switch (msg)
		{
		// what can empty the field or fill it: the edit repaints only what it changed,
		// so the placeholder needs a whole repaint to come or go
		case WM_CHAR: case WM_KEYDOWN: case WM_SETTEXT: case WM_PASTE: case WM_CUT: case WM_CLEAR:
		case WM_UNDO: case EM_UNDO: case EM_REPLACESEL: case WM_IME_CHAR: case WM_IME_ENDCOMPOSITION:
		{
			const bool empty = GetWindowTextLengthW(hwnd) == 0;
			if (empty != info->IsEmpty)
			{
				info->IsEmpty = empty;
				InvalidateRect(hwnd, NULL, TRUE);
			}
			break;
		}
		}
		return result;
	}

	HWND CreateTextEditMultiline(HWND hParent, HINSTANCE hInstance,
		const int& x, const int& y, const int& w, const int& h,
		const TextEditSettings& settings)
	{
		DWORD dwStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_WANTRETURN |
			(settings.TextAlign & (ES_LEFT | ES_CENTER | ES_RIGHT));
		if (settings.IsMultiline)
		{
			dwStyle |= ES_MULTILINE;
			if (settings.IsVscroll)
				dwStyle |= WS_VSCROLL;
		}
		else
		{
			dwStyle |= ES_AUTOHSCROLL;
		}

		HWND hwnd = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"", 
			dwStyle,
			x, y, w, h, hParent, NULL, hInstance, NULL);

		TextEditInfo* info = new TextEditInfo(settings);
		SendMessage(hwnd, WM_SETFONT, (WPARAM)info->hFont, TRUE);
		SetWindowSubclass(hwnd, _EditSubclassProc, 0, (DWORD_PTR)info);
		// WM_NCCALCSIZE ran at creation, before the subclass: once more, with it
		if (settings.IsVCenter && !settings.IsMultiline)
			SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
				SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

		return hwnd;
	}

	void TextEdit_SetPlaceholder(HWND hwnd, const std::wstring& placeholder)
	{
		TextEditInfo* info = nullptr;
		if (!GetWindowSubclass(hwnd, _EditSubclassProc, 0, (DWORD_PTR*)&info) || !info)
			return;
		info->Settings.Placeholder = placeholder;
		// tracked only while there is a placeholder: bring it up to date
		info->IsEmpty = GetWindowTextLengthW(hwnd) == 0;
		InvalidateRect(hwnd, NULL, TRUE);
	}

	ProgressBarSettings ProgressBarSettings::Create()
	{
		ProgressBarSettings settings;

		settings.Background = Color::MakeRGBA(236, 239, 245);
		settings.Background2 = Color::MakeRGBA(229, 232, 237);

		settings.Border = Color::MakeRGBA(207, 214, 230);

		settings.FillColor = Color::MakeRGBA(57, 114, 227);
		
		return settings;
	}

	class ProgressBarInfo
	{
	public:
		ProgressBarSettings Settings;

		int Progress = 0;
		int MinValue = 0;
		int MaxValue = 100;

	public:
		ProgressBarInfo(const ProgressBarSettings& settings)
		{
			Settings = settings;
		}

		~ProgressBarInfo()
		{
		}

		int GetPercent() const
		{
			if (MaxValue <= MinValue) return 0;
			return (Progress - MinValue) * 100 / (MaxValue - MinValue);
		}
	};

	LRESULT CALLBACK _ProgressBarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
	{
		ProgressBarInfo* info = (ProgressBarInfo*)dwRefData;

		switch (msg)
		{
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hwnd, &ps);

			RECT rc;
			GetClientRect(hwnd, &rc);

			COLORREF bgColor = RGB(info->Settings.Background.R, info->Settings.Background.G, info->Settings.Background.B);

			HBRUSH hBgBrush = CreateSolidBrush(bgColor);
			FillRect(hdc, &rc, hBgBrush);
			DeleteObject(hBgBrush);

			if (info->Settings.Background2.A != 0)
			{
				int h = info->Settings.Background2Height;
				if (h > 0 && h < (rc.bottom - rc.top))
				{
					RECT fillRect = rc;
					fillRect.bottom = rc.top + h;

					COLORREF bgColor2 = RGB(info->Settings.Background2.R, info->Settings.Background2.G, info->Settings.Background2.B);
					HBRUSH hBgBrush2 = CreateSolidBrush(bgColor2);
					FillRect(hdc, &fillRect, hBgBrush2);
					DeleteObject(hBgBrush2);
				}
			}

			if (info->Settings.BorderWidth > 0 && info->Settings.Border.A != 0)
			{
				RECT borderRect = rc;
				int ceilOffset = info->Settings.BorderWidth >> 1;
				int rbOffset = (info->Settings.BorderWidth + 1) & 0x01;

				borderRect.left += ceilOffset;
				borderRect.top += ceilOffset;
				borderRect.right -= (ceilOffset - rbOffset);
				borderRect.bottom -= (ceilOffset - rbOffset);

				HPEN hPen = CreatePen(PS_SOLID, info->Settings.BorderWidth, RGB(info->Settings.Border.R, info->Settings.Border.G, info->Settings.Border.B));
				HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
				HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
				Rectangle(hdc, borderRect.left, borderRect.top, borderRect.right, borderRect.bottom);
				SelectObject(hdc, hOldPen);
				SelectObject(hdc, hOldBrush);
				DeleteObject(hPen);
			}

			int percentage = info->GetPercent();
			if (percentage > 0)
			{
				RECT fillRect = rc;
				int fillWidth = (fillRect.right - fillRect.left) * percentage / 100;
				fillRect.right = fillRect.left + fillWidth;

				if (fillRect.right > fillRect.left)
				{
					COLORREF fillColor = RGB(info->Settings.FillColor.R, info->Settings.FillColor.G, info->Settings.FillColor.B);

					HBRUSH hFillBrush = CreateSolidBrush(fillColor);
					FillRect(hdc, &fillRect, hFillBrush);
					DeleteObject(hFillBrush);
				}
			}

			EndPaint(hwnd, &ps);
			return 0;
		}
		case WM_NCDESTROY:
		{
			delete info;
			RemoveWindowSubclass(hwnd, _ProgressBarProc, uIdSubclass);
			break;
		}
		}
		return DefSubclassProc(hwnd, msg, wParam, lParam);
	}

	HWND CreateProgressBar(HWND hParent, HINSTANCE hInstance,
		int x, int y, int w, int h,
		const ProgressBarSettings& settings)
	{
		HWND hwnd = CreateWindow(L"STATIC", L"",
			WS_CHILD | WS_VISIBLE,
			x, y, w, h, hParent, NULL, hInstance, NULL);

		ProgressBarInfo* info = new ProgressBarInfo(settings);
		SetWindowSubclass(hwnd, _ProgressBarProc, 0, (DWORD_PTR)info);

		return hwnd;
	}

	void ProgressBar_SetRange(HWND hwnd, int minVal, int maxVal)
	{
		ProgressBarInfo* info = nullptr;
		GetWindowSubclass(hwnd, _ProgressBarProc, 0, (DWORD_PTR*)&info);
		if (info)
		{
			info->MinValue = minVal;
			info->MaxValue = maxVal;
			InvalidateRect(hwnd, NULL, FALSE);
		}
	}

	void ProgressBar_SetPos(HWND hwnd, int pos)
	{
		ProgressBarInfo* info = nullptr;
		GetWindowSubclass(hwnd, _ProgressBarProc, 0, (DWORD_PTR*)&info);
		if (info)
		{
			info->Progress = max(info->MinValue, min(pos, info->MaxValue));
			InvalidateRect(hwnd, NULL, FALSE);
		}
	}

	int ProgressBar_GetPos(HWND hwnd)
	{
		ProgressBarInfo* info = nullptr;
		GetWindowSubclass(hwnd, _ProgressBarProc, 0, (DWORD_PTR*)&info);
		return info ? info->Progress : 0;
	}

	ComboBoxSettings ComboBoxSettings::Create()
	{
		ComboBoxSettings settings;

		settings.Background = Color::MakeRGBA(255, 255, 255);
		settings.BackgroundHover = Color::MakeRGBA(255, 255, 255);
		settings.BackgroundFocus = Color::MakeRGBA(255, 255, 255);

		settings.Border = Color::MakeRGBA(206, 215, 230);
		settings.BorderHover = Color::MakeRGBA(57, 114, 227);
		settings.BorderFocus = Color::MakeRGBA(57, 114, 227);

		settings.TextColor = Color::MakeRGBA(0, 0, 0);

		settings.ArrowColor = Color::MakeRGBA(0, 0, 0);
		settings.ArrowColorPressed = Color::MakeRGBA(0, 0, 0);

		settings.ArrowBackgroundHover = Color::MakeRGBA(232, 236, 245);
		settings.ArrowBackgroundPressed = Color::MakeRGBA(210, 218, 235);

		settings.DropdownBackground = Color::MakeRGBA(255, 255, 255);
		settings.DropdownBorder = Color::MakeRGBA(206, 215, 230);
		settings.DropdownTextColor = Color::MakeRGBA(0, 0, 0);

		settings.DropdownHoverColor = Color::MakeRGBA(218, 226, 242);
		settings.DropdownTextHoverColor = Color::MakeRGBA(0, 0, 0);

		return settings;
	}

	class ComboBoxInfo
	{
	public:
		ComboBoxSettings Settings;
		bool IsHovered;
		bool IsArrowHovered;
		bool IsArrowPressed;
		HFONT hFont;
		HWND hListBox;
		HWND hComboBox;
		std::vector<std::wstring> Items;
		int SelectedIndex;
		int HoveredIndex;
		HHOOK hMouseHook;

	public:
		ComboBoxInfo(const ComboBoxSettings& settings, const std::vector<std::wstring> items = {})
		{
			Settings = settings;
			IsHovered = false;
			IsArrowHovered = false;
			IsArrowPressed = false;
			SelectedIndex = -1;
			HoveredIndex = -1;
			hComboBox = NULL;
			hListBox = NULL;
			hMouseHook = NULL;

			Items = items;

			int fontH = Settings.FontSize;
			if (fontH > 0)
				fontH = -MulDiv(fontH, 96, 72);
			hFont = CreateFontW(
				fontH,
				0, 0, 0,
				Settings.FontBold ? FW_BOLD : FW_NORMAL,
				Settings.FontItalic ? TRUE : FALSE, FALSE, FALSE,
				DEFAULT_CHARSET,
				OUT_DEFAULT_PRECIS,
				CLIP_DEFAULT_PRECIS,
				DEFAULT_QUALITY,
				DEFAULT_PITCH | FF_SWISS,
				Settings.FontName.c_str()
			);
		}

		~ComboBoxInfo()
		{
			DeleteObject(hFont);
		}
	};

	static ComboBoxInfo* g_OpenComboBoxInfo = nullptr;

	LRESULT CALLBACK _ComboBoxListProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
	LRESULT CALLBACK _ComboBoxProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

	void CloseComboBoxDropdown(ComboBoxInfo* info)
	{
		if (info->hListBox && IsWindowVisible(info->hListBox))
		{
			ShowWindow(info->hListBox, SW_HIDE);

			if (info->hMouseHook)
			{
				UnhookWindowsHookEx(info->hMouseHook);
				info->hMouseHook = NULL;
			}

			if (g_OpenComboBoxInfo == info)
				g_OpenComboBoxInfo = nullptr;
		}
	}

	LRESULT CALLBACK _MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam)
	{
		if (nCode >= 0 && g_OpenComboBoxInfo != nullptr)
		{
			if (wParam == WM_LBUTTONDOWN || wParam == WM_NCLBUTTONDOWN)
			{
				MOUSEHOOKSTRUCT* pMouseStruct = (MOUSEHOOKSTRUCT*)lParam;
				if (pMouseStruct != NULL)
				{
					HWND hwndClicked = pMouseStruct->hwnd;

					if (hwndClicked != g_OpenComboBoxInfo->hComboBox &&
						hwndClicked != g_OpenComboBoxInfo->hListBox)
					{
						CloseComboBoxDropdown(g_OpenComboBoxInfo);
						g_OpenComboBoxInfo = nullptr;
					}
				}
			}
		}

		return CallNextHookEx(NULL, nCode, wParam, lParam);
	}

	void OpenComboBoxDropdown(HWND hwnd, ComboBoxInfo* info)
	{
		RECT rc;
		GetWindowRect(hwnd, &rc);

		int itemHeight = info->Settings.DropDownItemSize;
		int dropdownHeight = min((int)info->Items.size() * itemHeight + 4, 200);

		if (!info->hListBox)
		{
			info->hListBox = CreateWindowEx(
				WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
				L"LISTBOX", NULL,
				WS_POPUP | LBS_NOTIFY | LBS_OWNERDRAWFIXED | WS_VSCROLL,
				rc.left, rc.bottom,
				rc.right - rc.left, dropdownHeight,
				hwnd, NULL, GetModuleHandle(NULL), NULL
			);

			SendMessage(info->hListBox, WM_SETFONT, (WPARAM)info->hFont, TRUE);
			SendMessage(info->hListBox, LB_SETITEMHEIGHT, 0, itemHeight);

			SetWindowSubclass(info->hListBox, _ComboBoxListProc, 0, (DWORD_PTR)info);
		}

		SendMessage(info->hListBox, LB_RESETCONTENT, 0, 0);

		for (const auto& item : info->Items)
			SendMessage(info->hListBox, LB_ADDSTRING, 0, (LPARAM)item.c_str());

		if (info->SelectedIndex >= 0 && info->SelectedIndex < (int)info->Items.size())
			SendMessage(info->hListBox, LB_SETCURSEL, info->SelectedIndex, 0);


		SetWindowPos(info->hListBox, HWND_TOPMOST,
			rc.left, rc.bottom,
			rc.right - rc.left, dropdownHeight,
			SWP_NOACTIVATE | SWP_SHOWWINDOW);

		g_OpenComboBoxInfo = info;
		if (!info->hMouseHook)
		{
			info->hMouseHook = SetWindowsHookEx(WH_MOUSE, _MouseHookProc, NULL, GetCurrentThreadId());
		}
	}

	HWND CreateComboBox(HWND hParent, HMENU hMenu, HINSTANCE hInstance,
		int x, int y, int w, int h,
		const ComboBoxSettings& settings,
		const std::vector<std::wstring>& items)
	{
		HWND hwnd = CreateWindow(L"STATIC", L"",
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | SS_NOTIFY,
			x, y, w, h, hParent, hMenu, hInstance, NULL);

		ComboBoxInfo* info = new ComboBoxInfo(settings, items);
		if (0 == info->Settings.DropDownItemSize)
			info->Settings.DropDownItemSize = h;
		info->hComboBox = hwnd;
		if (info->Settings.StartSelectedIndex >= 0 && info->Settings.StartSelectedIndex < (int)items.size())
		{
			info->SelectedIndex = info->Settings.StartSelectedIndex;
		}

		SetWindowSubclass(hwnd, _ComboBoxProc, 0, (DWORD_PTR)info);
		return hwnd;
	}

	void ComboBox_AddItem(HWND hwnd, const std::wstring& text)
	{
		ComboBoxInfo* info = nullptr;
		GetWindowSubclass(hwnd, _ComboBoxProc, 0, (DWORD_PTR*)&info);
		if (info)
		{
			info->Items.push_back(text);
			InvalidateRect(hwnd, NULL, FALSE);
		}
	}
	void ComboBox_SetItems(HWND hwnd, const std::vector<std::wstring>& items, const int& selectedIndex)
	{
		ComboBoxInfo* info = nullptr;
		GetWindowSubclass(hwnd, _ComboBoxProc, 0, (DWORD_PTR*)&info);
		if (info)
		{
			info->Items = items;
			info->SelectedIndex = selectedIndex;
			InvalidateRect(hwnd, NULL, FALSE);
		}
	}
	void ComboBox_SetCurrent(HWND hwnd, int index)
	{
		ComboBoxInfo* info = nullptr;
		GetWindowSubclass(hwnd, _ComboBoxProc, 0, (DWORD_PTR*)&info);
		if (info && index >= 0 && index < (int)info->Items.size())
		{
			info->SelectedIndex = index;
			InvalidateRect(hwnd, NULL, FALSE);
		}
	}
	int ComboBox_GetCurrent(HWND hwnd)
	{
		ComboBoxInfo* info = nullptr;
		GetWindowSubclass(hwnd, _ComboBoxProc, 0, (DWORD_PTR*)&info);
		return info ? info->SelectedIndex : -1;
	}
	std::wstring ComboBox_GetCurrentText(HWND hwnd)
	{
		ComboBoxInfo* info = nullptr;
		GetWindowSubclass(hwnd, _ComboBoxProc, 0, (DWORD_PTR*)&info);
		if (info->SelectedIndex >= 0 && info->SelectedIndex < (int)info->Items.size())
			return info->Items[info->SelectedIndex];
		return L"";
	}
	std::vector<std::wstring> ComboBox_GetItems(HWND hwnd)
	{
		ComboBoxInfo* info = nullptr;
		GetWindowSubclass(hwnd, _ComboBoxProc, 0, (DWORD_PTR*)&info);
		if (info)
			return info->Items;
		return {};
	}

	// Proc
	LRESULT CALLBACK _ComboBoxProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
	{
		ComboBoxInfo* info = (ComboBoxInfo*)dwRefData;

		switch (msg)
		{
		case WM_MOUSEMOVE:
		{
			if (!info->IsHovered)
			{
				info->IsHovered = true;
				InvalidateRect(hwnd, NULL, FALSE);

				TRACKMOUSEEVENT tme = { sizeof(tme) };
				tme.dwFlags = TME_LEAVE;
				tme.hwndTrack = hwnd;
				TrackMouseEvent(&tme);
			}

			RECT rc;
			GetClientRect(hwnd, &rc);

			RECT rcArrow = rc;
			int offset = 3;
			int size = rc.bottom - rc.top - 2 * offset;

			rcArrow.top = rc.top + offset;
			rcArrow.bottom = rc.bottom - offset;
			rcArrow.right = rc.right - offset;
			rcArrow.left = rcArrow.right - size;

			POINT pt;
			GetCursorPos(&pt);
			ScreenToClient(hwnd, &pt);

			bool wasArrowHovered = info->IsArrowHovered;
			info->IsArrowHovered = (pt.x >= rcArrow.left);

			if (wasArrowHovered != info->IsArrowHovered)
				InvalidateRect(hwnd, NULL, FALSE);
			break;
		}
		case WM_MOUSELEAVE:
		{
			info->IsHovered = false;
			info->IsArrowHovered = false;
			InvalidateRect(hwnd, NULL, FALSE);
			break;
		}
		case WM_SETFOCUS:
		{
			InvalidateRect(hwnd, NULL, FALSE);
			break;
		}
		case WM_ENABLE:
		{
			// The STATIC underneath repaints itself on enable/disable directly,
			// not through WM_PAINT - as an empty static over the combo box,
			// which then stays blank until something invalidates it. Repaint
			// the combo box instead.
			if (!wParam && info->hListBox && IsWindowVisible(info->hListBox))
				CloseComboBoxDropdown(info);
			info->IsHovered = false;
			info->IsArrowHovered = false;
			info->IsArrowPressed = false;
			InvalidateRect(hwnd, NULL, FALSE);
			return 0;
		}
		case WM_KILLFOCUS:
		{
			if (info->hListBox && GetFocus() != info->hListBox)
				CloseComboBoxDropdown(info);
			InvalidateRect(hwnd, NULL, FALSE);
			break;
		}
		case WM_LBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
		{
			SetFocus(hwnd);

			RECT rc;
			GetClientRect(hwnd, &rc);
			
			RECT rcArrow = rc;
			int offset = 3;
			int size = rc.bottom - rc.top - 2 * offset;

			rcArrow.top = rc.top + offset;
			rcArrow.bottom = rc.bottom - offset;
			rcArrow.right = rc.right - offset;
			rcArrow.left = rcArrow.right - size;

			POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

			if (pt.x >= rcArrow.left)
			{
				info->IsArrowPressed = true;
				InvalidateRect(hwnd, NULL, FALSE);
			}

			if (info->hListBox && IsWindowVisible(info->hListBox))
			{
				CloseComboBoxDropdown(info);
				info->IsArrowPressed = false;
				InvalidateRect(hwnd, NULL, FALSE);
			}
			else
			{
				info->HoveredIndex = info->SelectedIndex;
				OpenComboBoxDropdown(hwnd, info);
			}

			return 0;
		}
		case WM_LBUTTONUP:
		{
			info->IsArrowPressed = false;
			InvalidateRect(hwnd, NULL, FALSE);
			break;
		}
		case WM_GETDLGCODE:
		{
			return DLGC_WANTARROWS | DLGC_WANTALLKEYS;
		}
		case WM_KEYDOWN:
		{
			switch (wParam)
			{
			case VK_UP:
			case VK_LEFT:
				if (info->SelectedIndex > 0)
				{
					info->SelectedIndex--;
					info->HoveredIndex = info->SelectedIndex;
					InvalidateRect(hwnd, NULL, FALSE);

					HWND hParent = GetParent(hwnd);
					SendMessage(hParent, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), COMBOBOX_SELECTED_INDEX_CHANDED), (LPARAM)hwnd);

					if (info->hListBox && IsWindowVisible(info->hListBox))
					{
						InvalidateRect(info->hListBox, NULL, FALSE);
					}
				}
				return 0;

			case VK_DOWN:
			case VK_RIGHT:
				if (info->SelectedIndex < (int)info->Items.size() - 1)
				{
					info->SelectedIndex++;
					info->HoveredIndex = info->SelectedIndex;
					InvalidateRect(hwnd, NULL, FALSE);

					HWND hParent = GetParent(hwnd);
					SendMessage(hParent, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), COMBOBOX_SELECTED_INDEX_CHANDED), (LPARAM)hwnd);

					if (info->hListBox && IsWindowVisible(info->hListBox))
					{
						InvalidateRect(info->hListBox, NULL, FALSE);
					}
				}
				return 0;

			case VK_SPACE:
			case VK_RETURN:
				if (!info->hListBox || !IsWindowVisible(info->hListBox))
				{
					info->HoveredIndex = info->SelectedIndex;
					OpenComboBoxDropdown(hwnd, info);
				}
				else
				{
					CloseComboBoxDropdown(info);
				}
				return 0;

			case VK_ESCAPE:
				CloseComboBoxDropdown(info);
				return 0;
			case VK_TAB:
				HWND hParent = GetParent(hwnd);
				HWND hNext = GetNextDlgTabItem(hParent, hwnd, GetKeyState(VK_SHIFT) < 0);
				SetFocus(hNext);
				return 0;
			}
			break;
		}
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hwnd, &ps);

			RECT rc;
			GetClientRect(hwnd, &rc);

			RECT rcArrow = rc;
			int offset = 3;
			int size = rc.bottom - rc.top - 2 * offset;

			rcArrow.top = rc.top + offset;
			rcArrow.bottom = rc.bottom - offset;
			rcArrow.right = rc.right - offset;
			rcArrow.left = rcArrow.right - size;

			bool hasFocus = (GetFocus() == hwnd);
			
			Color& backColor = hasFocus ? info->Settings.BackgroundFocus : (info->IsHovered ? info->Settings.BackgroundHover : info->Settings.Background);
			COLORREF bgColor = RGB(backColor.R, backColor.G, backColor.B);

			RECT mainRect = rc;
			HBRUSH hBrush = CreateSolidBrush(bgColor);
			FillRect(hdc, &mainRect, hBrush);
			DeleteObject(hBrush);

			Color* arrowBgColor = nullptr;
			if (info->IsArrowPressed)
				arrowBgColor = &info->Settings.ArrowBackgroundPressed;
			else if (info->IsArrowHovered)
				arrowBgColor = &info->Settings.ArrowBackgroundHover;

			if (arrowBgColor && arrowBgColor->A != 0)
			{
				HBRUSH hArrowBrush = CreateSolidBrush(RGB(arrowBgColor->R, arrowBgColor->G, arrowBgColor->B));
				FillRect(hdc, &rcArrow, hArrowBrush);
				DeleteObject(hArrowBrush);
			}

			bool isUseFocus = hasFocus || (info->Settings.BorderHoverAsFocus && info->IsHovered);
			Color& borderColor = isUseFocus ? info->Settings.BorderFocus : (info->IsHovered ? info->Settings.BorderHover : info->Settings.Border);

			if (borderColor.A != 0)
			{
				int borderW = isUseFocus ? 2 : 1;

				HPEN hPen = CreatePen(PS_SOLID, borderW, RGB(borderColor.R, borderColor.G, borderColor.B));
				HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
				HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));

				RECT borderRect = rc;
				int ceilOffset = borderW >> 1;
				int rbOffset = (borderW + 1) & 0x01;

				borderRect.left += ceilOffset;
				borderRect.top += ceilOffset;
				borderRect.right -= (ceilOffset - rbOffset);
				borderRect.bottom -= (ceilOffset - rbOffset);

				Rectangle(hdc, borderRect.left, borderRect.top, borderRect.right, borderRect.bottom);

				SelectObject(hdc, hOldPen);
				SelectObject(hdc, hOldBrush);
				DeleteObject(hPen);
			}

			// Draw dropdown arrow
			Color& arrowColor = info->IsArrowPressed ? info->Settings.ArrowColorPressed : info->Settings.ArrowColor;
			if (arrowColor.A != 0)
			{
				HPEN hArrowPen = CreatePen(PS_SOLID, 1, RGB(arrowColor.R, arrowColor.G, arrowColor.B));
				HPEN hOldPen = (HPEN)SelectObject(hdc, hArrowPen);

				int maxLine = (((rcArrow.right - rcArrow.left) / 4) * 2) + 1;

				int cx = rcArrow.left + (rcArrow.right - rcArrow.left) / 2;
				int cy = rcArrow.top + (rcArrow.bottom - rcArrow.top) / 2;

				cy += ((maxLine / 4) + 1);

				if (info->IsArrowPressed)
					cy += info->Settings.ArrowPressedOffset;

				for (int i = 1; i <= maxLine; i += 2)
				{
					MoveToEx(hdc, cx, cy, NULL);
					LineTo(hdc, cx + i, cy);

					--cx;
					--cy;
				}

				SelectObject(hdc, hOldPen);
				DeleteObject(hArrowPen);
			}

			// Draw selected text - halfway to the background while disabled
			if (info->SelectedIndex >= 0 && info->SelectedIndex < (int)info->Items.size())
			{
				SetBkMode(hdc, TRANSPARENT);
				const Color& text = info->Settings.TextColor;
				if (IsWindowEnabled(hwnd))
					SetTextColor(hdc, RGB(text.R, text.G, text.B));
				else
					SetTextColor(hdc, RGB((text.R + backColor.R) / 2, (text.G + backColor.G) / 2,
						(text.B + backColor.B) / 2));

				HFONT hOldFont = (HFONT)SelectObject(hdc, info->hFont);

				RECT textRect = mainRect;
				textRect.left += 5;
				textRect.right -= 5;

				DrawText(hdc, info->Items[info->SelectedIndex].c_str(), -1, &textRect,
					DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

				SelectObject(hdc, hOldFont);
			}
			// nothing selected: the placeholder, in the grey of the edits' one
			else if (!info->Settings.Placeholder.empty())
			{
				SetBkMode(hdc, TRANSPARENT);
				SetTextColor(hdc, RGB(130, 130, 130));
				HFONT hOldFont = (HFONT)SelectObject(hdc, info->hFont);
				RECT textRect = mainRect;
				textRect.left += 5;
				textRect.right -= 5;
				DrawText(hdc, info->Settings.Placeholder.c_str(), -1, &textRect,
					DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
				SelectObject(hdc, hOldFont);
			}

			EndPaint(hwnd, &ps);
			return 0;
		}		
		case WM_DRAWITEM:
		{
			LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;

			if (dis->hwndItem == info->hListBox && dis->CtlType == ODT_LISTBOX)
			{
				if (dis->itemID == (UINT)-1)
					return TRUE;

				bool isSelected = (dis->itemState & ODS_SELECTED) || (dis->itemID == info->SelectedIndex);
				bool isHovered = (dis->itemID == (UINT)info->HoveredIndex);

				COLORREF bgColor;
				COLORREF textColor;

				if (isHovered)
				{
					bgColor = RGB(info->Settings.DropdownHoverColor.R,
						info->Settings.DropdownHoverColor.G,
						info->Settings.DropdownHoverColor.B);
					textColor = RGB(info->Settings.DropdownTextColor.R,
						info->Settings.DropdownTextColor.G,
						info->Settings.DropdownTextColor.B);
				}
				else
				{
					bgColor = RGB(info->Settings.DropdownBackground.R,
						info->Settings.DropdownBackground.G,
						info->Settings.DropdownBackground.B);
					textColor = RGB(info->Settings.DropdownTextColor.R,
						info->Settings.DropdownTextColor.G,
						info->Settings.DropdownTextColor.B);
				}

				HBRUSH hBrush = CreateSolidBrush(bgColor);
				FillRect(dis->hDC, &dis->rcItem, hBrush);
				DeleteObject(hBrush);

				const wchar_t* text = info->Items[dis->itemID].c_str();

				SetBkMode(dis->hDC, TRANSPARENT);
				SetTextColor(dis->hDC, textColor);

				HFONT hOldFont = (HFONT)SelectObject(dis->hDC, info->hFont);

				RECT textRect = dis->rcItem;
				textRect.left += 5;
				textRect.right -= 5;

				DrawText(dis->hDC, text, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

				SelectObject(dis->hDC, hOldFont);

				return TRUE;
			}
			break;
		}
		case WM_COMMAND:
		{
			// Handle selection change from listbox
			if (HIWORD(wParam) == LBN_SELCHANGE && (HWND)lParam == info->hListBox)
			{
				// This is handled in WM_LBUTTONUP of listbox now
			}
			break;
		}
		case (WM_USER + 100):
		{
			InvalidateRect(hwnd, NULL, FALSE);

			HWND hParent = GetParent(hwnd);
			if (IsWindow(hParent))
			{
				SendMessage(hParent, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), COMBOBOX_SELECTED_INDEX_CHANDED), (LPARAM)hwnd);
			}
			return 0;
		}
		case WM_NCDESTROY:
		{
			if (info->hMouseHook)
			{
				UnhookWindowsHookEx(info->hMouseHook);
				info->hMouseHook = NULL;
			}

			if (g_OpenComboBoxInfo == info)
				g_OpenComboBoxInfo = nullptr;
			
			if (info->hListBox)
				DestroyWindow(info->hListBox);
			delete info;
			RemoveWindowSubclass(hwnd, _ComboBoxProc, uIdSubclass);
			break;
		}
		}
		return DefSubclassProc(hwnd, msg, wParam, lParam);
	}

	LRESULT CALLBACK _ComboBoxListProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
	{
		ComboBoxInfo* info = (ComboBoxInfo*)dwRefData;

		switch (msg)
		{
		case WM_KILLFOCUS:
		{
			ShowWindow(hwnd, SW_HIDE);
			break;
		}		
		case WM_LBUTTONDOWN:
		{
			SetCapture(hwnd);
			POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			int idx = (int)SendMessage(hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y));
			if (HIWORD(idx) == 0)
			{
				SendMessage(hwnd, LB_SETCURSEL, LOWORD(idx), 0);
				info->HoveredIndex = LOWORD(idx);
				InvalidateRect(hwnd, NULL, FALSE);
			}
			return 0;
		}
		case WM_LBUTTONUP:
		{
			ReleaseCapture();
			int idx = (int)SendMessage(hwnd, LB_GETCURSEL, 0, 0);
			if (idx != LB_ERR)
			{
				info->SelectedIndex = idx;
				HWND hCombo = info->hComboBox;

				SendMessage(hCombo, WM_USER + 100, idx, 0);
			}
			ShowWindow(hwnd, SW_HIDE);
			return 0;
		}
		case WM_MOUSEMOVE:
		{
			POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			int idx = (int)SendMessage(hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y));
			if (HIWORD(idx) == 0) // inside listbox
			{
				int oldHover = info->HoveredIndex;
				info->HoveredIndex = LOWORD(idx);
				if (oldHover != info->HoveredIndex)
				{
					InvalidateRect(hwnd, NULL, FALSE);
				}
			}
			break;
		}
		case WM_MOUSELEAVE:
		{
			info->HoveredIndex = -1;
			InvalidateRect(hwnd, NULL, FALSE);
			break;
		}		
		case WM_NCDESTROY:
		{
			RemoveWindowSubclass(hwnd, _ComboBoxListProc, uIdSubclass);
			break;
		}
		case WM_GETDLGCODE:
		{
			return DLGC_WANTARROWS | DLGC_WANTCHARS;
		}
		case WM_PAINT:
		{
			LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);

			HDC hdc = GetDC(hwnd);

			RECT rc;
			GetClientRect(hwnd, &rc);

			Color& borderColor = info->Settings.DropdownBorder;
			if (borderColor.A != 0)
			{
				int borderW = 1;

				HPEN hPen = CreatePen(PS_SOLID, borderW, RGB(borderColor.R, borderColor.G, borderColor.B));
				HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
				HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));

				RECT borderRect = rc;
				int ceilOffset = borderW >> 1;
				int rbOffset = (borderW + 1) & 0x01;

				borderRect.left += ceilOffset;
				borderRect.top += ceilOffset;
				borderRect.right -= (ceilOffset - rbOffset);
				borderRect.bottom -= (ceilOffset - rbOffset);

				Rectangle(hdc, borderRect.left, borderRect.top, borderRect.right, borderRect.bottom);

				SelectObject(hdc, hOldPen);
				SelectObject(hdc, hOldBrush);
				DeleteObject(hPen);
			}

			ReleaseDC(hwnd, hdc);
			return result;
		}
		case WM_MOUSEACTIVATE:
		{
			return MA_NOACTIVATE;
		}
		}
		return DefSubclassProc(hwnd, msg, wParam, lParam);
	}

	LabelSettings LabelSettings::Create(const LabelType& type, const DWORD& align)
	{
		LabelSettings settings;
		settings.TextColor = (type == LabelType::Enabled) ? Color::MakeRGBA(0, 0, 0) : Color::MakeRGBA(130, 130, 130);
		settings.Background = Color::GetDefaultWindowBackground();
		settings.Alignment = align;
		return settings;
	}

	class LabelInfo
	{
	public:
		LabelSettings Settings;
		HFONT hFont;

		LabelInfo(const LabelSettings& settings)
		{
			Settings = settings;

			int fontH = Settings.FontSize;
			if (fontH > 0)
				fontH = -MulDiv(fontH, 96, 72);
			hFont = CreateFontW(
				fontH,
				0, 0, 0,
				Settings.FontBold ? FW_BOLD : FW_NORMAL,
				Settings.FontItalic ? TRUE : FALSE, FALSE, FALSE,
				DEFAULT_CHARSET,
				OUT_DEFAULT_PRECIS,
				CLIP_DEFAULT_PRECIS,
				DEFAULT_QUALITY,
				DEFAULT_PITCH | FF_SWISS,
				Settings.FontName.c_str()
			);
		}

		~LabelInfo()
		{
			DeleteObject(hFont);
		}
	};

	LRESULT CALLBACK _LabelProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
	{
		LabelInfo* info = (LabelInfo*)dwRefData;

		switch (msg)
		{
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hwnd, &ps);

			RECT rc;
			GetClientRect(hwnd, &rc);

			if (info->Settings.Background.A != 0)
			{
				COLORREF bgColor = RGB(info->Settings.Background.R, info->Settings.Background.G, info->Settings.Background.B);
				HBRUSH hBrush = CreateSolidBrush(bgColor);
				FillRect(hdc, &rc, hBrush);
				DeleteObject(hBrush);
			}
			else
			{
				HWND hParent = GetParent(hwnd);
				if (hParent)
				{
					HDC hdcParent = GetDC(hParent);
					POINT pt = { 0, 0 };
					MapWindowPoints(hwnd, hParent, &pt, 1);

					BitBlt(hdc, 0, 0, rc.right, rc.bottom, hdcParent, pt.x, pt.y, SRCCOPY);
					ReleaseDC(hParent, hdcParent);
				}
			}

			std::wstring text = Label_GetText(hwnd);

			SetBkMode(hdc, TRANSPARENT);
			SetTextColor(hdc, RGB(info->Settings.TextColor.R, info->Settings.TextColor.G, info->Settings.TextColor.B));

			HFONT hOldFont = (HFONT)SelectObject(hdc, info->hFont);

			// DrawText centres a single line only: wrapping text asked for in the middle is
			// measured and moved there, and starts at the top when it is taller than the label
			const UINT format = info->Settings.Alignment;
			if ((format & DT_VCENTER) && !(format & DT_SINGLELINE))
			{
				RECT measured = rc;
				DrawText(hdc, text.c_str(), -1, &measured, (format & ~DT_VCENTER) | DT_CALCRECT);
				const int room = (rc.bottom - rc.top) - (measured.bottom - measured.top);
				if (room > 0)
					rc.top += room / 2;
			}
			DrawText(hdc, text.c_str(), -1, &rc, format);

			SelectObject(hdc, hOldFont);
			EndPaint(hwnd, &ps);
			return 0;
		}
		case WM_NCDESTROY:
		{
			delete info;
			RemoveWindowSubclass(hwnd, _LabelProc, uIdSubclass);
			break;
		}
		}
		return DefSubclassProc(hwnd, msg, wParam, lParam);
	}

	HWND CreateLabel(HWND hParent, HINSTANCE hInstance,
		LPCWSTR text,
		const int& x, const int& y, const int& w, const int& h,
		const LabelSettings& settings)
	{
		HWND hwnd = CreateWindow(L"STATIC", text,
			WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
			x, y, w, h, hParent, NULL, hInstance, NULL);

		LabelInfo* info = new LabelInfo(settings);
		SetWindowSubclass(hwnd, _LabelProc, 0, (DWORD_PTR)info);

		return hwnd;
	}

	void Label_SetText(HWND hwnd, const std::wstring& text)
	{
		SetWindowText(hwnd, text.c_str());
		InvalidateRect(hwnd, NULL, FALSE);
	}
	std::wstring Label_GetText(HWND hwnd)
	{
		int len = GetWindowTextLength(hwnd);
		if (len == 0)
			return L"";

		std::wstring text(len, L'\0');
		GetWindowText(hwnd, &text[0], len + 1);
		return text;
	}

	void Label_SetTextColor(HWND hwnd, const Color& textColor)
	{
		LabelInfo* info = nullptr;
		GetWindowSubclass(hwnd, _LabelProc, 0, (DWORD_PTR*)&info);
		if (info)
		{
			info->Settings.TextColor = textColor;
			InvalidateRect(hwnd, NULL, TRUE);
		}
	}

	void Label_SetTextAndColor(HWND hwnd, const std::wstring& text, const Color& textColor)
	{
		LabelInfo* info = nullptr;
		GetWindowSubclass(hwnd, _LabelProc, 0, (DWORD_PTR*)&info);
		if (info)
		{
			info->Settings.TextColor = textColor;
			SetWindowText(hwnd, text.c_str());
			InvalidateRect(hwnd, NULL, TRUE);
		}
	}

	CheckBoxSettings CheckBoxSettings::Create()
	{
		CheckBoxSettings settings;
		settings.Background = Color::GetDefaultWindowBackground();
		settings.Box = Color::MakeRGBA(255, 255, 255);
		// the TextEdit border colours, the button accent
		settings.Border = Color::MakeRGBA(206, 215, 230);
		settings.BorderHover = Color::MakeRGBA(57, 114, 227);
		settings.BorderFocus = Color::MakeRGBA(57, 114, 227);
		settings.Check = Color::MakeRGBA(57, 114, 227);
		settings.Tick = Color::MakeRGBA(255, 255, 255);
		// the Enabled and Disabled label colours
		settings.TextColor = Color::MakeRGBA(0, 0, 0);
		settings.TextColorDisabled = Color::MakeRGBA(130, 130, 130);
		return settings;
	}

	// What a screen reader hears. The checkbox is an owner-drawn BUTTON, which the system
	// accessibility proxy reports as a push button without a state; Dynamic Annotation
	// overrides the role once and asks this server for the state each time.
	class CheckBoxAccessibility : public IAccPropServer
	{
		LONG m_refs = 1;
		IAccPropServices* m_services;

	public:
		explicit CheckBoxAccessibility(IAccPropServices* services) : m_services(services)
		{
			m_services->AddRef();
		}
		~CheckBoxAccessibility()
		{
			m_services->Release();
		}
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override
		{
			if (!out)
				return E_POINTER;
			if (riid == IID_IUnknown || riid == IID_IAccPropServer)
			{
				*out = static_cast<IAccPropServer*>(this);
				AddRef();
				return S_OK;
			}
			*out = NULL;
			return E_NOINTERFACE;
		}
		ULONG STDMETHODCALLTYPE AddRef() override
		{
			return InterlockedIncrement(&m_refs);
		}
		ULONG STDMETHODCALLTYPE Release() override
		{
			const ULONG refs = InterlockedDecrement(&m_refs);
			if (!refs)
				delete this;
			return refs;
		}
		HRESULT STDMETHODCALLTYPE GetPropValue(const BYTE* key, DWORD keyLength, MSAAPROPID property,
			VARIANT* value, BOOL* found) override
		{
			*found = FALSE;
			VariantInit(value);
			HWND hwnd = NULL;
			DWORD object = 0, child = 0;
			if (FAILED(m_services->DecomposeHwndIdentityString(key, keyLength, &hwnd, &object, &child)) || !IsWindow(hwnd))
				return S_OK;
			const bool checked = SendMessage(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
			if (property == PROPID_ACC_STATE)
			{
				LONG state = STATE_SYSTEM_FOCUSABLE;
				if (checked) state |= STATE_SYSTEM_CHECKED;
				// focused as the system reports it: in the window the user is in
				if (GetFocus() == hwnd && GetForegroundWindow() == GetAncestor(hwnd, GA_ROOT))
					state |= STATE_SYSTEM_FOCUSED;
				if (!IsWindowEnabled(hwnd)) state |= STATE_SYSTEM_UNAVAILABLE;
				if (!IsWindowVisible(hwnd)) state |= STATE_SYSTEM_INVISIBLE;
				value->vt = VT_I4;
				value->lVal = state;
				*found = TRUE;
			}
			else if (property == PROPID_ACC_DEFAULTACTION)
			{
				// the words a standard checkbox uses
				value->vt = VT_BSTR;
				value->bstrVal = SysAllocString(checked ? L"Uncheck" : L"Check");
				*found = value->bstrVal != NULL;
			}
			return S_OK;
		}
	};

	class CheckBoxInfo
	{
	public:
		CheckBoxSettings Settings;
		bool IsHovered = false;
		bool IsChecked = false;
		bool IsSpaceDown = false;
		IAccPropServices* Accessibility = nullptr;
		HFONT hFont;

		CheckBoxInfo(const CheckBoxSettings& settings, bool checked)
		{
			Settings = settings;
			IsChecked = checked;
			int fontH = Settings.FontSize;
			if (fontH > 0)
				fontH = -MulDiv(fontH, 96, 72);
			hFont = CreateFontW(fontH, 0, 0, 0, Settings.FontBold ? FW_BOLD : FW_NORMAL,
				Settings.FontItalic ? TRUE : FALSE, FALSE, FALSE, DEFAULT_CHARSET,
				OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
				DEFAULT_PITCH | FF_SWISS, Settings.FontName.c_str());
		}

		~CheckBoxInfo()
		{
			DeleteObject(hFont);
		}
	};

	// The tick, smoothed: GDI draws lines without anti-aliasing, so a 2 px tick in a 16 px
	// box comes out as a jagged blob. Drawn here 4x larger with round ends into a mask,
	// each pixel then takes the average of its 4x4 block as the share of tick colour.
	static void _DrawSmoothTick(HDC hdc, const RECT& box, const Color& fill, const Color& tick)
	{
		const int size = box.right - box.left;
		const int scale = 4, big = size * scale;
		BITMAPINFO bi = {};
		bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bi.bmiHeader.biWidth = big;
		bi.bmiHeader.biHeight = -big;
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;
		void* bits = NULL;
		HDC maskDc = CreateCompatibleDC(hdc);
		HBITMAP mask = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
		if (!maskDc || !mask || !bits)
		{
			if (mask) DeleteObject(mask);
			if (maskDc) DeleteDC(maskDc);
			return;
		}
		HBITMAP oldMask = (HBITMAP)SelectObject(maskDc, mask);
		RECT all = { 0, 0, big, big };
		FillRect(maskDc, &all, (HBRUSH)GetStockObject(BLACK_BRUSH));

		// in 1/16 of the box: down to the lower third, then up to the top right; 2 px wide
		const float points[3][2] = { { 4.0f, 8.5f }, { 7.0f, 11.5f }, { 12.0f, 5.0f } };
		LOGBRUSH brush = { BS_SOLID, RGB(255, 255, 255), 0 };
		HPEN pen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND | PS_JOIN_ROUND,
			(DWORD)(2.0f * big / 16.0f + 0.5f), &brush, 0, NULL);
		HPEN oldPen = (HPEN)SelectObject(maskDc, pen);
		POINT line[3];
		for (int i = 0; i < 3; ++i)
		{
			line[i].x = (LONG)(points[i][0] * big / 16.0f + 0.5f);
			line[i].y = (LONG)(points[i][1] * big / 16.0f + 0.5f);
		}
		Polyline(maskDc, line, 3);
		SelectObject(maskDc, oldPen);
		DeleteObject(pen);
		GdiFlush();

		const DWORD* pixels = (const DWORD*)bits;
		for (int y = 0; y < size; ++y)
		{
			for (int x = 0; x < size; ++x)
			{
				int sum = 0;
				for (int sy = 0; sy < scale; ++sy)
					for (int sx = 0; sx < scale; ++sx)
						sum += pixels[(y * scale + sy) * big + x * scale + sx] & 0xFF;
				const int a = sum / (scale * scale);
				if (!a)
					continue;
				SetPixel(hdc, box.left + x, box.top + y, RGB(
					fill.R + (tick.R - fill.R) * a / 255,
					fill.G + (tick.G - fill.G) * a / 255,
					fill.B + (tick.B - fill.B) * a / 255));
			}
		}
		SelectObject(maskDc, oldMask);
		DeleteObject(mask);
		DeleteDC(maskDc);
	}

	static void _CheckBoxSetChecked(HWND hwnd, CheckBoxInfo* info, bool checked)
	{
		if (info->IsChecked == checked)
			return;
		info->IsChecked = checked;
		InvalidateRect(hwnd, NULL, FALSE);
		// a screen reader says "checked" / "not checked" now, not on the next look
		NotifyWinEvent(EVENT_OBJECT_STATECHANGE, hwnd, OBJID_CLIENT, CHILDID_SELF);
	}

	static void _CheckBoxToggle(HWND hwnd, CheckBoxInfo* info)
	{
		_CheckBoxSetChecked(hwnd, info, !info->IsChecked);
	}

	static const MSAAPROPID _CheckBoxDynamicProps[] = { PROPID_ACC_STATE, PROPID_ACC_DEFAULTACTION };

	static void _CheckBoxAnnotate(HWND hwnd, CheckBoxInfo* info)
	{
		// needs COM on this thread, as every AVS host has it; without it the checkbox
		// still works, a screen reader just takes it for a button
		IAccPropServices* services = NULL;
		if (FAILED(CoCreateInstance(CLSID_AccPropServices, NULL, CLSCTX_INPROC_SERVER,
			IID_IAccPropServices, (void**)&services)) || !services)
			return;
		VARIANT role;
		VariantInit(&role);
		role.vt = VT_I4;
		role.lVal = ROLE_SYSTEM_CHECKBUTTON;
		services->SetHwndProp(hwnd, OBJID_CLIENT, CHILDID_SELF, PROPID_ACC_ROLE, role);
		CheckBoxAccessibility* server = new CheckBoxAccessibility(services);
		services->SetHwndPropServer(hwnd, OBJID_CLIENT, CHILDID_SELF, _CheckBoxDynamicProps,
			ARRAYSIZE(_CheckBoxDynamicProps), server, ANNO_THIS);
		server->Release();
		info->Accessibility = services;
	}

	static void _CheckBoxClearAnnotation(HWND hwnd, CheckBoxInfo* info)
	{
		if (!info->Accessibility)
			return;
		const MSAAPROPID props[] = { PROPID_ACC_ROLE, PROPID_ACC_STATE, PROPID_ACC_DEFAULTACTION };
		info->Accessibility->ClearHwndProps(hwnd, OBJID_CLIENT, CHILDID_SELF, props, ARRAYSIZE(props));
		info->Accessibility->Release();
		info->Accessibility = nullptr;
	}

	LRESULT CALLBACK _CheckBoxProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
	{
		CheckBoxInfo* info = (CheckBoxInfo*)dwRefData;

		switch (msg)
		{
		case BM_GETCHECK:
			return info->IsChecked ? BST_CHECKED : BST_UNCHECKED;
		case BM_SETCHECK:
			_CheckBoxSetChecked(hwnd, info, wParam == BST_CHECKED);
			return 0;
		case WM_LBUTTONUP:
		{
			// released over the control after pressing it: the button sends BN_CLICKED
			// on its own, the state flips first
			if (SendMessage(hwnd, BM_GETSTATE, 0, 0) & BST_PUSHED)
				_CheckBoxToggle(hwnd, info);
			break;
		}
		case WM_LBUTTONDBLCLK:
			// an owner-drawn button takes the second click of a double click for a
			// BN_DOUBLECLICKED, not a press; a standard checkbox flips twice
			return DefSubclassProc(hwnd, WM_LBUTTONDOWN, wParam, lParam);
		case WM_GETDLGCODE:
		{
			if (lParam)
			{
				MSG* pMsg = (MSG*)lParam;
				if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_SPACE)
					return DLGC_WANTALLKEYS;
			}
			return 0;
		}
		case WM_KEYDOWN:
		{
			// as a standard checkbox: Space flips it once, on release; the key's
			// auto-repeat while it is held does nothing
			if (wParam == VK_SPACE)
			{
				info->IsSpaceDown = true;
				return 0;
			}
			break;
		}
		case WM_KEYUP:
		{
			if (wParam == VK_SPACE)
			{
				if (info->IsSpaceDown)
				{
					info->IsSpaceDown = false;
					_CheckBoxToggle(hwnd, info);
					SendMessage(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), BN_CLICKED), (LPARAM)hwnd);
				}
				return 0;
			}
			break;
		}
		case WM_MOUSEMOVE:
		{
			if (!info->IsHovered)
			{
				info->IsHovered = true;
				InvalidateRect(hwnd, NULL, FALSE);
				TRACKMOUSEEVENT tme = { sizeof(tme) };
				tme.dwFlags = TME_LEAVE;
				tme.hwndTrack = hwnd;
				TrackMouseEvent(&tme);
			}
			break;
		}
		case WM_MOUSELEAVE:
		{
			info->IsHovered = false;
			InvalidateRect(hwnd, NULL, FALSE);
			break;
		}
		case WM_KILLFOCUS:
			// Space pressed here and released elsewhere is no click
			info->IsSpaceDown = false;
			InvalidateRect(hwnd, NULL, FALSE);
			break;
		case WM_SETFOCUS:
		case WM_ENABLE:
		{
			InvalidateRect(hwnd, NULL, FALSE);
			break;
		}
		case WM_ERASEBKGND:
			return 1;
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC paintDc = BeginPaint(hwnd, &ps);
			RECT rc;
			GetClientRect(hwnd, &rc);

			// drawn off screen, then copied: no flicker on hover
			HDC hdc = CreateCompatibleDC(paintDc);
			HBITMAP bitmap = CreateCompatibleBitmap(paintDc, rc.right, rc.bottom);
			HBITMAP oldBitmap = (HBITMAP)SelectObject(hdc, bitmap);

			const CheckBoxSettings& s = info->Settings;
			HBRUSH back = CreateSolidBrush(RGB(s.Background.R, s.Background.G, s.Background.B));
			FillRect(hdc, &rc, back);
			DeleteObject(back);

			const bool enabled = IsWindowEnabled(hwnd) != FALSE;
			const bool focused = GetFocus() == hwnd;
			RECT box;
			box.left = rc.left + 1;
			box.top = rc.top + (rc.bottom - rc.top - s.BoxSize) / 2;
			box.right = box.left + s.BoxSize;
			box.bottom = box.top + s.BoxSize;

			Color fill = info->IsChecked ? s.Check : s.Box;
			Color border = info->IsChecked ? s.Check : s.Border;
			int borderW = 1;
			if (focused) { border = s.BorderFocus; borderW = 2; }
			else if (info->IsHovered) border = s.BorderHover;
			if (!enabled)
			{
				// greyed: the box keeps its shape, without the accent
				fill = info->IsChecked ? Color::MakeRGBA(170, 178, 192) : Color::MakeRGBA(245, 246, 249);
				border = Color::MakeRGBA(200, 205, 214);
				borderW = 1;
			}

			HBRUSH fillBrush = CreateSolidBrush(RGB(fill.R, fill.G, fill.B));
			HPEN borderPen = CreatePen(PS_INSIDEFRAME, borderW, RGB(border.R, border.G, border.B));
			HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, fillBrush);
			HPEN oldPen = (HPEN)SelectObject(hdc, borderPen);
			Rectangle(hdc, box.left, box.top, box.right, box.bottom);
			SelectObject(hdc, oldPen);
			SelectObject(hdc, oldBrush);
			DeleteObject(borderPen);
			DeleteObject(fillBrush);

			if (info->IsChecked)
				_DrawSmoothTick(hdc, box, fill, s.Tick);

			wchar_t text[512];
			GetWindowText(hwnd, text, 512);
			RECT textRect = rc;
			textRect.left = box.right + s.TextGap;
			const Color& textColor = enabled ? s.TextColor : s.TextColorDisabled;
			SetBkMode(hdc, TRANSPARENT);
			SetTextColor(hdc, RGB(textColor.R, textColor.G, textColor.B));
			HFONT oldFont = (HFONT)SelectObject(hdc, info->hFont);
			DrawText(hdc, text, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			SelectObject(hdc, oldFont);

			BitBlt(paintDc, 0, 0, rc.right, rc.bottom, hdc, 0, 0, SRCCOPY);
			SelectObject(hdc, oldBitmap);
			DeleteObject(bitmap);
			DeleteDC(hdc);
			EndPaint(hwnd, &ps);
			return 0;
		}
		case WM_NCDESTROY:
		{
			_CheckBoxClearAnnotation(hwnd, info);
			delete info;
			RemoveWindowSubclass(hwnd, _CheckBoxProc, uIdSubclass);
			break;
		}
		}
		return DefSubclassProc(hwnd, msg, wParam, lParam);
	}

	HWND CreateCheckBox(HWND hParent, HMENU hMenu, HINSTANCE hInstance,
		LPCWSTR text,
		const int& x, const int& y, const int& w, const int& h,
		const CheckBoxSettings& settings, bool checked)
	{
		// owner-drawn, so Windows draws nothing of its own; _CheckBoxProc paints it all
		HWND hwnd = CreateWindow(L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
			x, y, w, h, hParent, hMenu, hInstance, NULL);
		CheckBoxInfo* info = new CheckBoxInfo(settings, checked);
		SendMessage(hwnd, WM_SETFONT, (WPARAM)info->hFont, TRUE);
		SetWindowSubclass(hwnd, _CheckBoxProc, 0, (DWORD_PTR)info);
		_CheckBoxAnnotate(hwnd, info);
		return hwnd;
	}
}