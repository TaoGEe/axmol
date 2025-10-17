#ifndef __UICONFIG_H__
#define __UICONFIG_H__

#include "cocos2d.h"
#include "FairyGUIMacros.h"

NS_FGUI_BEGIN

class UIConfig
{
public:
    enum class Theme
    {
        DEFAULT,
        DARK,
        LIGHT
    };
public:
    static std::string defaultFont;
    static std::string buttonSound;
    static float buttonSoundVolumeScale;
    static int defaultScrollStep;
    static float defaultScrollDecelerationRate;
    static bool defaultScrollTouchEffect;
    static bool defaultScrollBounceEffect;
    static ScrollBarDisplayType defaultScrollBarDisplay;
    static std::string verticalScrollBar;
    static std::string horizontalScrollBar;
    static int touchDragSensitivity;
    static int clickDragSensitivity;
    static int touchScrollSensitivity;
    static int defaultComboBoxVisibleItemCount;
    static std::string globalModalWaiting;
    static ax::Color4F modalLayerColor;
    static std::string tooltipsWin;
    static bool bringWindowToFrontOnClick;
    static std::string windowModalWaiting;
    static std::string popupMenu;
    static std::string popupMenu_seperator;
    static Theme defaultTheme;

    static void registerFont(const std::string& aliasName, const std::string& realName);
    static const std::string& getRealFontName(const std::string& aliasName, bool* isTTF = nullptr);

    static void convertToThemeColor( ax::Color4B& color ) {
        if (defaultTheme == Theme::DARK && color.r == color.g && color.r == color.b) {
            color.r = color.g = color.b = 255 - color.r;
        }
    }
    static void convertToThemeColor( ax::Color3B& color ) {
        if (defaultTheme == Theme::DARK && color.r == color.g && color.r == color.b) {
            color.r = color.g = color.b = 255 - color.r;
        }
    }

private:
    struct FontNameItem
    {
        std::string name;
        bool ttf;
    };
    static std::unordered_map<std::string, FontNameItem> _fontNames;
};

NS_FGUI_END

#endif
