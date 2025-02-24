/****************************************************************************
Copyright (c) 2011      Laschweinski
Copyright (c) 2013-2016 Chukong Technologies Inc.
Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.

http://www.cocos2d-x.org

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
****************************************************************************/

#include "platform/PlatformConfig.h"
#if AX_TARGET_PLATFORM == AX_PLATFORM_WASM

#include "platform/wasm/Application-wasm.h"
#include <unistd.h>
#include <sys/time.h>
#include <string>
#include "base/Director.h"
#include "base/Utils.h"
#include "platform/FileUtils.h"
#include <emscripten/emscripten.h>

NS_AX_BEGIN


// sharedApplication pointer
Application * Application::sm_pSharedApplication = nullptr;

static long getCurrentMillSecond() {
    long lLastTime;
    struct timeval stCurrentTime;

    gettimeofday(&stCurrentTime,NULL);
    lLastTime = stCurrentTime.tv_sec*1000+stCurrentTime.tv_usec*0.001; // milliseconds
    return lLastTime;
}

Application::Application()
: _animationInterval(1.0f/60.0f*1000.0f)
{
    AX_ASSERT(! sm_pSharedApplication);
    sm_pSharedApplication = this;
}

Application::~Application()
{
    AX_ASSERT(this == sm_pSharedApplication);
    sm_pSharedApplication = nullptr;
}

extern "C" void mainLoopIter(void)
{
    auto director = Director::getInstance();
    auto glview = director->getOpenGLView();

    director->mainLoop();
    glview->pollEvents();
}

int Application::run()
{
    initGLContextAttrs();
    // Initialize instance and cocos2d.
    if (! applicationDidFinishLaunching())
    {
        return 1;
    }

    auto director = Director::getInstance();
    auto glview = director->getOpenGLView();

    // Retain glview to avoid glview being released in the while loop
    glview->retain();

    emscripten_set_main_loop(&mainLoopIter, 0, 1);
    // TODO: ? does these cleanup really run?
    /* Only work on Desktop
    *  Director::mainLoop is really one frame logic
    *  when we want to close the window, we should call Director::end();
    *  then call Director::mainLoop to do release of internal resources
    */
    if (glview->isOpenGLReady())
    {
        director->end();
        director->mainLoop();
        director = nullptr;
    }
    glview->release();
    return 0;
}

void Application::setAnimationInterval(float interval)
{
    _animationInterval = interval*1000.0f;
}

void Application::setResourceRootPath(const std::string& rootResDir)
{
    _resourceRootPath = rootResDir;
    if (_resourceRootPath[_resourceRootPath.length() - 1] != '/')
    {
        _resourceRootPath += '/';
    }
    FileUtils* pFileUtils = FileUtils::getInstance();
    std::vector<std::string> searchPaths = pFileUtils->getSearchPaths();
    searchPaths.insert(searchPaths.begin(), _resourceRootPath);
    pFileUtils->setSearchPaths(searchPaths);
}

const std::string& Application::getResourceRootPath()
{
    return _resourceRootPath;
}

Application::Platform Application::getTargetPlatform()
{
    return Platform::Emscripten;
}

std::string Application::getVersion()
{
    return "";
}

bool Application::openURL(std::string_view url)
{
    EM_ASM_ARGS({
        window.open(UTF8ToString($0));
    }, url.data());

    return true;
}

//////////////////////////////////////////////////////////////////////////
// static member function
//////////////////////////////////////////////////////////////////////////
Application* Application::getInstance()
{
    AX_ASSERT(sm_pSharedApplication);
    return sm_pSharedApplication;
}

// @deprecated Use getInstance() instead
Application* Application::sharedApplication()
{
    return Application::getInstance();
}

const char * Application::getCurrentLanguageCode()
{
    static char code[3]={0};
    char pLanguageName[16];

    EM_ASM_ARGS({
        var lang = localStorage.getItem('localization_language');
        if (lang == null) {
            stringToUTF8(window.navigator.language.replace(/-.*/, ""), $0, 16);
        } else {
            stringToUTF8(lang, $0, 16);
        }
    }, pLanguageName);
    strncpy(code,pLanguageName,2);
    code[2]='\0';
    return code;
}

LanguageType Application::getCurrentLanguage()
{
    char pLanguageName[16];

    EM_ASM_ARGS({
        var lang = localStorage.getItem('localization_language');
        if (lang == null) {
            stringToUTF8(window.navigator.language.replace(/-.*/, ""), $0, 16);
        } else {
            stringToUTF8(lang, $0, 16);
        }
    }, pLanguageName);

    return utils::getLanguageTypeByISO2(pLanguageName);
}

NS_AX_END

#endif // AX_TARGET_PLATFORM == AX_PLATFORM_WASM

