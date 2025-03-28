/*
* cocos2d-x   https://axmolengine.github.io/
*
* Copyright (c) 2010-2014 - cocos2d-x community
* Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.
*
* Portions Copyright (c) Microsoft Open Technologies, Inc.
* All Rights Reserved
*
* Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an
* "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and limitations under the License.
*/

#pragma once

#include "OpenGLES.h"
#include "OpenGLESPage.g.h"
#include <memory>
#include <condition_variable>
#include <mutex>
#include <concrt.h>
#include <concurrent_queue.h>

#include "AxmolRenderer.h"

namespace AxmolAppWinRT
{
    public ref class OpenGLESPage sealed
    {
    public:
        OpenGLESPage();
        virtual ~OpenGLESPage();
        void SetVisibility(bool isVisible);

    internal:
        OpenGLESPage(OpenGLES* openGLES);

        void ProcessOperations();

    private:
        void OnPageLoaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
        void OnVisibilityChanged(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::VisibilityChangedEventArgs^ args);
#if (WINAPI_FAMILY == WINAPI_FAMILY_PHONE_APP) || _MSC_VER >= 1900
        void OnBackButtonPressed(Platform::Object ^ sender, Windows::UI::Core::BackRequestedEventArgs ^ args);
#endif
        void CreateRenderSurface();
        void DestroyRenderSurface();
        void RecoverFromLostDevice();
        void TerminateApp();
        void StartRenderLoop();
        void StopRenderLoop();

        void CreateInput();

        OpenGLES* mOpenGLES;
        std::shared_ptr<AxmolRenderer> mRenderer;

        EGLSurface mRenderSurface;     // This surface is associated with a swapChainPanel on the page
        Concurrency::critical_section mRenderSurfaceCriticalSection;
        Windows::Foundation::IAsyncAction^ mRenderLoopWorker;

        // Track user input on a background worker thread.
        Windows::Foundation::IAsyncAction^ mInputLoopWorker;
        Windows::UI::Core::CoreIndependentInputSource^ mCoreInput;

        // Independent touch and pen handling functions.
        void OnPointerPressed(Platform::Object^ sender, Windows::UI::Core::PointerEventArgs^ e);
        void OnPointerMoved(Platform::Object^ sender, Windows::UI::Core::PointerEventArgs^ e);
        void OnPointerReleased(Platform::Object^ sender, Windows::UI::Core::PointerEventArgs^ e);
        void OnPointerWheelChanged(Platform::Object^ sender, Windows::UI::Core::PointerEventArgs^ e);

        // Independent keyboard handling functions.
		void OnKeyPressed(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::KeyEventArgs^ args);
		void OnKeyReleased(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::KeyEventArgs^ args);

		void OnCharacterReceived(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::CharacterReceivedEventArgs^ args);

        void OnOrientationChanged(Windows::Graphics::Display::DisplayInformation^ sender, Platform::Object^ args);

        float mDpi;
        bool mDeviceLost;
        bool mVisible;
        bool mCursorVisible;
        Windows::Graphics::Display::DisplayOrientations mOrientation;

        std::mutex mSleepMutex;

    internal:
        std::condition_variable mSleepCondition;

        Concurrency::concurrent_queue<std::function<void()>> mOperations;
    };
}
