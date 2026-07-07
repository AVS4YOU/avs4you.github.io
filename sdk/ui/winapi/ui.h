#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace AVS
{
	enum class Buttons
	{
		Primary = 0,
		Default = 1,
		ToggleGroupDisable = 2,
		ToggleGroupEnable = 3,

		Undefined = 255
	};

	class Color
	{
	public:
		unsigned char R = 255;
		unsigned char G = 255;
		unsigned char B = 255;
		unsigned char A = 255;

		Color() {}
		Color(const Color&) = default;

		static Color MakeRGBA(const unsigned char& R, const unsigned char& G, const unsigned char& B, const unsigned char& A = 255)
		{
			Color c;
			c.R = R;
			c.G = G;
			c.B = B;
			c.A = A;
			return c;
		}

		static Color GetDefaultWindowBackground()
		{
			return MakeRGBA(236, 239, 245);
		}
	};

	class ButtonSettings
	{
	public:
		Color Background;
		Color Border;

		Color BackgroundHover;
		Color BorderHover;

		Color BackgroundPressed;
		Color BorderPressed;

		Color FocusColor;
		int FocusWidth = 2;

		Color TextColor;
		Color TextColorPressed;
		int TextPressedOffset = 2;

		std::wstring FontName = L"Segoe UI";
		bool FontItalic = false;
		bool FontBold = false;
		int FontSize = -12;

		ButtonSettings() {}
		ButtonSettings(const ButtonSettings&) = default;

		static ButtonSettings Create(const Buttons& type);
	};

	HWND CreateButton(HWND hParent, HMENU hMenu, HINSTANCE hInstance, 
		LPCWSTR text,
		const int& x, const int& y, const int& w, const int& h, 
		const ButtonSettings& settings);

	void Button_SetSettings(HWND hwnd, const ButtonSettings& settings, const std::wstring& text = L"");

	class TextEditSettings
	{
	public:
		Color Border;
		Color BorderHover;
		Color BorderFocus;
		int BorderWidth = 1;
		int BorderHoverWidth = 2;
		int BorderFocusWidth = 2;

		bool IsVscroll = false;
		bool IsMultiline = false;

		std::wstring FontName = L"Segoe UI";
		bool FontItalic = false;
		bool FontBold = false;
		int FontSize = -12;

		static TextEditSettings Create();
	};

	HWND CreateTextEditMultiline(HWND hParent, HINSTANCE hInstance,
		const int& x, const int& y, const int& w, const int& h,
		const TextEditSettings& settings);

	class ProgressBarSettings
	{
	public:
		Color Background;

		Color Background2;
		int Background2Height = 4;

		Color Border;
		int BorderWidth = 1;

		Color FillColor;

		ProgressBarSettings() {}
		ProgressBarSettings(const ProgressBarSettings&) = default;

		static ProgressBarSettings Create();
	};

	HWND CreateProgressBar(HWND hParent, HINSTANCE hInstance,
		int x, int y, int w, int h,
		const ProgressBarSettings& settings);

	void ProgressBar_SetRange(HWND hwnd, int minVal, int maxVal);
	void ProgressBar_SetPos(HWND hwnd, int pos);
	int ProgressBar_GetPos(HWND hwnd);

	class ComboBoxSettings
	{
	public:
		Color Background;
		Color BackgroundHover;
		Color BackgroundFocus;

		Color Border;
		Color BorderHover;
		Color BorderFocus;

		bool BorderHoverAsFocus = true;
		
		Color TextColor;
		
		Color ArrowColor;
		Color ArrowColorPressed;

		Color ArrowBackgroundHover;
		Color ArrowBackgroundPressed;
		int ArrowPressedOffset = 1;

		Color DropdownBackground;
		Color DropdownBorder;
		Color DropdownTextColor;

		Color DropdownTextHoverColor;
		Color DropdownHoverColor;

		std::wstring FontName = L"Segoe UI";
		bool FontItalic = false;
		bool FontBold = false;
		int FontSize = -12;

		int DropDownItemSize = 0; // 0 = comboHeight
		int StartSelectedIndex = 0;

		static ComboBoxSettings Create();
	};
	
	const int COMBOBOX_SELECTED_INDEX_CHANDED = 0x8000;
	HWND CreateComboBox(HWND hParent, HMENU hMenu, HINSTANCE hInstance,
		int x, int y, int w, int h,
		const ComboBoxSettings& settings,
		const std::vector<std::wstring>& items = {});

	void ComboBox_AddItem(HWND hwnd, const std::wstring& text);
	void ComboBox_SetItems(HWND hwnd, const std::vector<std::wstring>& items, const int& selectedIndex = -1);
	void ComboBox_SetCurrent(HWND hwnd, int index);
	int ComboBox_GetCurrent(HWND hwnd);
	std::wstring ComboBox_GetCurrentText(HWND hwnd);
	std::vector<std::wstring> ComboBox_GetItems(HWND hwnd);

	enum class LabelType
	{
		Enabled = 0,
		Disabled = 1,

		Undefined = 255
	};

	class LabelSettings
	{
	public:
		Color TextColor;
		Color Background;

		std::wstring FontName = L"Segoe UI";
		int FontSize = -12;
		bool FontBold = false;
		bool FontItalic = false;

		DWORD Alignment = DT_LEFT | DT_VCENTER | DT_SINGLELINE;

		static LabelSettings Create(const LabelType& type, const DWORD& align = (DT_LEFT | DT_VCENTER | DT_SINGLELINE));
	};

	HWND CreateLabel(HWND hParent, HINSTANCE hInstance,
		LPCWSTR text,
		const int& x, const int& y, const int& w, const int& h,
		const LabelSettings& settings);

	void Label_SetText(HWND hwnd, const std::wstring& text);
	std::wstring Label_GetText(HWND hwnd);

	void Label_SetTextColor(HWND hwnd, const Color& textColor);
	void Label_SetTextAndColor(HWND hwnd, const std::wstring& text, const Color& textColor);
}