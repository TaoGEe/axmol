/****************************************************************************
 Copyright (c) 2013      Zynga Inc.
 Copyright (c) 2013-2016 Chukong Technologies Inc.
 Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.

 https://axmolengine.github.io/

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

#include "2d/Label.h"
#include <algorithm>
#include <stddef.h>  // offsetof
#include "base/Types.h"
#include "2d/Font.h"
#include "2d/FontAtlasCache.h"
#include "2d/FontAtlas.h"
#include "2d/Sprite.h"
#include "2d/SpriteBatchNode.h"
#include "2d/DrawNode.h"
#include "2d/Camera.h"
#include "base/UTF8.h"
#include "base/Macros.h"
#include "platform/FileUtils.h"
#include "renderer/Renderer.h"
#include "renderer/RenderCommand.h"
#include "base/Director.h"
#include "base/EventListenerCustom.h"
#include "base/EventDispatcher.h"
#include "base/EventCustom.h"
#include "base/Utils.h"
#include "2d/FontFNT.h"
#include "renderer/Shaders.h"
#include "renderer/backend/ProgramState.h"
#include "renderer/backend/ProgramStateRegistry.h"

NS_AX_BEGIN

namespace
{
void updateBlend(backend::BlendDescriptor& blendDescriptor, BlendFunc blendFunc)
{
    blendDescriptor.blendEnabled = true;
    if (blendFunc == BlendFunc::ALPHA_NON_PREMULTIPLIED)
    {
        blendDescriptor.sourceRGBBlendFactor        = backend::BlendFactor::SRC_ALPHA;
        blendDescriptor.destinationRGBBlendFactor   = backend::BlendFactor::ONE_MINUS_SRC_ALPHA;
        blendDescriptor.sourceAlphaBlendFactor      = backend::BlendFactor::SRC_ALPHA;
        blendDescriptor.destinationAlphaBlendFactor = backend::BlendFactor::ONE_MINUS_SRC_ALPHA;
    }
    else
    {
        blendDescriptor.sourceRGBBlendFactor        = backend::BlendFactor::ONE;
        blendDescriptor.destinationRGBBlendFactor   = backend::BlendFactor::ONE_MINUS_SRC_ALPHA;
        blendDescriptor.sourceAlphaBlendFactor      = backend::BlendFactor::ONE;
        blendDescriptor.destinationAlphaBlendFactor = backend::BlendFactor::ONE_MINUS_SRC_ALPHA;
    }
}
}  // namespace

/**
 * LabelLetter used to update the quad in texture atlas without SpriteBatchNode.
 */
class LabelLetter : public Sprite
{
public:
    LabelLetter()
    {
        _textureAtlas  = nullptr;
        _letterVisible = true;
    }

    static LabelLetter* createWithTexture(Texture2D* texture, const Rect& rect, bool rotated = false)
    {
        auto letter = new LabelLetter();
        if (letter->initWithTexture(texture, rect, rotated))
        {
            letter->Sprite::setVisible(false);
            letter->autorelease();
            return letter;
        }
        AX_SAFE_DELETE(letter);
        return nullptr;
    }

    CREATE_FUNC(LabelLetter);

    virtual void updateTransform() override
    {
        if (isDirty())
        {
            _transformToBatch = getNodeToParentTransform();
            Vec2& size        = _rect.size;

            float x1 = _offsetPosition.x;
            float y1 = _offsetPosition.y;
            float x2 = x1 + size.width;
            float y2 = y1 + size.height;

            // issue #17022: don't flip, again, the letter since they are flipped in sprite's code
            // if (_flippedX) std::swap(x1, x2);
            // if (_flippedY) std::swap(y1, y2);

            float x = _transformToBatch.m[12];
            float y = _transformToBatch.m[13];

            float cr  = _transformToBatch.m[0];
            float sr  = _transformToBatch.m[1];
            float cr2 = _transformToBatch.m[5];
            float sr2 = -_transformToBatch.m[4];
            float ax  = x1 * cr - y1 * sr2 + x;
            float ay  = x1 * sr + y1 * cr2 + y;

            float bx = x2 * cr - y1 * sr2 + x;
            float by = x2 * sr + y1 * cr2 + y;
            float cx = x2 * cr - y2 * sr2 + x;
            float cy = x2 * sr + y2 * cr2 + y;
            float dx = x1 * cr - y2 * sr2 + x;
            float dy = x1 * sr + y2 * cr2 + y;

            _quad.bl.vertices.set(SPRITE_RENDER_IN_SUBPIXEL(ax), SPRITE_RENDER_IN_SUBPIXEL(ay), _positionZ);
            _quad.br.vertices.set(SPRITE_RENDER_IN_SUBPIXEL(bx), SPRITE_RENDER_IN_SUBPIXEL(by), _positionZ);
            _quad.tl.vertices.set(SPRITE_RENDER_IN_SUBPIXEL(dx), SPRITE_RENDER_IN_SUBPIXEL(dy), _positionZ);
            _quad.tr.vertices.set(SPRITE_RENDER_IN_SUBPIXEL(cx), SPRITE_RENDER_IN_SUBPIXEL(cy), _positionZ);

            if (_textureAtlas)
            {
                _textureAtlas->updateQuad(_quad, _atlasIndex);
            }

            _recursiveDirty = false;
            setDirty(false);
        }

        Node::updateTransform();
    }

    virtual void updateColor() override
    {
        if (_textureAtlas == nullptr)
        {
            return;
        }

        auto displayedOpacity = _displayedOpacity;
        if (!_letterVisible)
        {
            displayedOpacity = 0.0f;
        }
        Color4B color4(_displayedColor.r, _displayedColor.g, _displayedColor.b, displayedOpacity);
        // special opacity for premultiplied textures
        if (_opacityModifyRGB)
        {
            color4.r *= displayedOpacity / 255.0f;
            color4.g *= displayedOpacity / 255.0f;
            color4.b *= displayedOpacity / 255.0f;
        }
        _quad.bl.colors = color4;
        _quad.br.colors = color4;
        _quad.tl.colors = color4;
        _quad.tr.colors = color4;

        _textureAtlas->updateQuad(_quad, _atlasIndex);
    }

    void setVisible(bool visible) override
    {
        _letterVisible = visible;
        updateColor();
    }

    bool isVisible() const override { return _letterVisible; }

    // LabelLetter doesn't need to draw directly.
    void draw(Renderer* /*renderer*/, const Mat4& /*transform*/, uint32_t /*flags*/) override {}

private:
    bool _letterVisible;
};

Label::BatchCommand::BatchCommand()
{
    textCommand.setDrawType(CustomCommand::DrawType::ELEMENT);
    textCommand.setPrimitiveType(CustomCommand::PrimitiveType::TRIANGLE);
    shadowCommand.setDrawType(CustomCommand::DrawType::ELEMENT);
    shadowCommand.setPrimitiveType(CustomCommand::PrimitiveType::TRIANGLE);
    outLineCommand.setDrawType(CustomCommand::DrawType::ELEMENT);
    outLineCommand.setPrimitiveType(CustomCommand::PrimitiveType::TRIANGLE);
}

Label::BatchCommand::~BatchCommand()
{
    AX_SAFE_RELEASE(textCommand.getPipelineDescriptor().programState);
    AX_SAFE_RELEASE(shadowCommand.getPipelineDescriptor().programState);
    AX_SAFE_RELEASE(outLineCommand.getPipelineDescriptor().programState);
}

void Label::BatchCommand::setProgramState(backend::ProgramState* programState)
{
    assert(programState);

    auto& programStateText = textCommand.getPipelineDescriptor().programState;
    AX_SAFE_RELEASE(programStateText);
    programStateText = programState->clone();

    auto& programStateShadow = shadowCommand.getPipelineDescriptor().programState;
    AX_SAFE_RELEASE(programStateShadow);
    programStateShadow = programState->clone();

    auto& programStateOutline = outLineCommand.getPipelineDescriptor().programState;
    AX_SAFE_RELEASE(programStateOutline);
    programStateOutline = programState->clone();
}

std::array<CustomCommand*, 3> Label::BatchCommand::getCommandArray()
{
    return std::array<CustomCommand*, 3>{&textCommand, &shadowCommand, &outLineCommand};
}

Label* Label::create()
{
    auto ret = new Label;
    ret->autorelease();
    return ret;
}

Label* Label::createWithSystemFont(std::string_view text,
                                   std::string_view font,
                                   float fontSize,
                                   const Vec2& dimensions /* = Vec2::ZERO */,
                                   TextHAlignment hAlignment /* = TextHAlignment::LEFT */,
                                   TextVAlignment vAlignment /* = TextVAlignment::TOP */)
{
    auto ret = new Label(hAlignment, vAlignment);

    ret->setSystemFontName(font);
    ret->setSystemFontSize(fontSize);
    ret->setDimensions(dimensions.width, dimensions.height);
    ret->setString(text);

    ret->autorelease();

    return ret;
}

Label* Label::createWithTTF(std::string_view text,
                            std::string_view fontFile,
                            float fontSize,
                            const Vec2& dimensions /* = Vec2::ZERO */,
                            TextHAlignment hAlignment /* = TextHAlignment::LEFT */,
                            TextVAlignment vAlignment /* = TextVAlignment::TOP */)
{
    auto ret = new Label(hAlignment, vAlignment);

    if (ret->initWithTTF(text, fontFile, fontSize, dimensions, hAlignment, vAlignment))
    {
        ret->autorelease();
        return ret;
    }

    AX_SAFE_DELETE(ret);
    return nullptr;
}

Label* Label::createWithTTF(const TTFConfig& ttfConfig,
                            std::string_view text,
                            TextHAlignment hAlignment /* = TextHAlignment::CENTER */,
                            int maxLineWidth /* = 0 */)
{
    auto ret = new Label(hAlignment);

    if (ret->initWithTTF(ttfConfig, text, hAlignment, maxLineWidth))
    {
        ret->autorelease();
        return ret;
    }

    AX_SAFE_DELETE(ret);
    return nullptr;
}

Label* Label::createWithBMFont(std::string_view bmfontPath,
                               std::string_view text,
                               const TextHAlignment& hAlignment,
                               int maxLineWidth)
{
    auto ret = new Label(hAlignment);

    if (ret->setBMFontFilePath(bmfontPath))
    {
        ret->setMaxLineWidth(maxLineWidth);
        ret->setString(text);
        ret->autorelease();

        return ret;
    }

    delete ret;
    return nullptr;
}

Label* Label::createWithBMFont(std::string_view bmfontPath,
                               std::string_view text,
                               const TextHAlignment& hAlignment,
                               int maxLineWidth,
                               const Rect& imageRect,
                               bool imageRotated)
{
    auto ret = new Label(hAlignment);

    if (ret->setBMFontFilePath(bmfontPath, imageRect, imageRotated))
    {
        ret->setMaxLineWidth(maxLineWidth);
        ret->setString(text);
        ret->autorelease();

        return ret;
    }

    delete ret;
    return nullptr;
}

Label* Label::createWithBMFont(std::string_view bmfontPath,
                               std::string_view text,
                               const TextHAlignment& hAlignment,
                               int maxLineWidth,
                               std::string_view subTextureKey)
{
    auto ret = new Label(hAlignment);

    if (ret->setBMFontFilePath(bmfontPath, subTextureKey))
    {
        ret->setMaxLineWidth(maxLineWidth);
        ret->setString(text);
        ret->autorelease();

        return ret;
    }

    delete ret;
    return nullptr;
}

Label* Label::createWithBMFont(std::string_view bmfontPath,
                               std::string_view text,
                               const TextHAlignment& hAlignment,
                               int maxLineWidth,
                               const Vec2& imageOffset)
{
    return createWithBMFont(bmfontPath, text, hAlignment, maxLineWidth, Rect(imageOffset.x, imageOffset.y, 0, 0),
                            false);
}

Label* Label::createWithCharMap(std::string_view plistFile)
{
    auto ret = new Label();

    if (ret->setCharMap(plistFile))
    {
        ret->autorelease();
        return ret;
    }

    delete ret;
    return nullptr;
}

Label* Label::createWithCharMap(Texture2D* texture, int itemWidth, int itemHeight, int startCharMap)
{
    auto ret = new Label();

    if (ret->setCharMap(texture, itemWidth, itemHeight, startCharMap))
    {
        ret->autorelease();
        return ret;
    }

    delete ret;
    return nullptr;
}

Label* Label::createWithCharMap(std::string_view charMapFile, int itemWidth, int itemHeight, int startCharMap)
{
    auto ret = new Label();

    if (ret->setCharMap(charMapFile, itemWidth, itemHeight, startCharMap))
    {
        ret->autorelease();
        return ret;
    }

    delete ret;
    return nullptr;
}

bool Label::setCharMap(std::string_view plistFile)
{
    auto newAtlas = FontAtlasCache::getFontAtlasCharMap(plistFile);

    if (!newAtlas)
    {
        reset();
        return false;
    }

    _currentLabelType = LabelType::CHARMAP;
    setFontAtlas(newAtlas);

    return true;
}

bool Label::initWithTTF(std::string_view text,
                        std::string_view fontFilePath,
                        float fontSize,
                        const Vec2& dimensions,
                        TextHAlignment /*hAlignment*/,
                        TextVAlignment /*vAlignment*/)
{
    if (FileUtils::getInstance()->isFileExist(fontFilePath))
    {
        TTFConfig ttfConfig(fontFilePath, fontSize, GlyphCollection::DYNAMIC);
        if (setTTFConfig(ttfConfig))
        {
            setDimensions(dimensions.width, dimensions.height);
            setString(text);
        }
        return true;
    }
    return false;
}

bool Label::initWithTTF(const TTFConfig& ttfConfig,
                        std::string_view text,
                        TextHAlignment /*hAlignment*/,
                        int maxLineWidth)
{
    if (FileUtils::getInstance()->isFileExist(ttfConfig.fontFilePath) && setTTFConfig(ttfConfig))
    {
        setMaxLineWidth(maxLineWidth);
        setString(text);
        return true;
    }
    return false;
}

bool Label::setCharMap(Texture2D* texture, int itemWidth, int itemHeight, int startCharMap)
{
    auto newAtlas = FontAtlasCache::getFontAtlasCharMap(texture, itemWidth, itemHeight, startCharMap);

    if (!newAtlas)
    {
        reset();
        return false;
    }

    _currentLabelType = LabelType::CHARMAP;
    setFontAtlas(newAtlas);

    return true;
}

bool Label::setCharMap(std::string_view charMapFile, int itemWidth, int itemHeight, int startCharMap)
{
    auto newAtlas = FontAtlasCache::getFontAtlasCharMap(charMapFile, itemWidth, itemHeight, startCharMap);

    if (!newAtlas)
    {
        reset();
        return false;
    }

    _currentLabelType = LabelType::CHARMAP;
    setFontAtlas(newAtlas);

    return true;
}

Label::Label(TextHAlignment hAlignment /* = TextHAlignment::LEFT */,
             TextVAlignment vAlignment /* = TextVAlignment::TOP */)
    : _textSprite(nullptr)
    , _shadowNode(nullptr)
    , _fontAtlas(nullptr)
    , _reusedLetter(nullptr)
    , _horizontalKernings(nullptr)
    , _boldEnabled(false)
    , _underlineNode(nullptr)
    , _strikethroughEnabled(false)
{
    setAnchorPoint(Vec2::ANCHOR_MIDDLE);
    reset();
    _hAlignment = hAlignment;
    _vAlignment = vAlignment;

#if AX_LABEL_DEBUG_DRAW
    _debugDrawNode = DrawNode::create();
    addChild(_debugDrawNode);
#endif

    _purgeTextureListener = EventListenerCustom::create(FontAtlas::CMD_PURGE_FONTATLAS, [this](EventCustom* event) {
        if (_fontAtlas && _currentLabelType == LabelType::TTF && event->getUserData() == _fontAtlas)
        {
            for (auto&& it : _letters)
            {
                it.second->setTexture(nullptr);
            }
            _batchNodes.clear();
            _batchCommands.clear();

            if (_fontAtlas)
            {
                FontAtlasCache::releaseFontAtlas(_fontAtlas);
            }
        }
    });
    _eventDispatcher->addEventListenerWithFixedPriority(_purgeTextureListener, 1);

    _resetTextureListener = EventListenerCustom::create(FontAtlas::CMD_RESET_FONTATLAS, [this](EventCustom* event) {
        if (_fontAtlas && _currentLabelType == LabelType::TTF && event->getUserData() == _fontAtlas)
        {
            _fontAtlas      = nullptr;
            auto lineHeight = _lineHeight;
            this->setTTFConfig(_fontConfig);
            if (_currentLabelType != LabelType::STRING_TEXTURE)
            {
                setLineHeight(lineHeight);
            }
            for (auto&& it : _letters)
            {
                getLetter(it.first);
            }
        }
    });
    _eventDispatcher->addEventListenerWithFixedPriority(_resetTextureListener, 2);
}

Label::~Label()
{
    delete[] _horizontalKernings;

    if (_fontAtlas)
    {
        Node::removeAllChildrenWithCleanup(true);
        AX_SAFE_RELEASE_NULL(_reusedLetter);
        _batchNodes.clear();
        FontAtlasCache::releaseFontAtlas(_fontAtlas);
    }
    _batchCommands.clear();
    _eventDispatcher->removeEventListener(_purgeTextureListener);
    _eventDispatcher->removeEventListener(_resetTextureListener);

    AX_SAFE_RELEASE_NULL(_textSprite);
    AX_SAFE_RELEASE_NULL(_shadowNode);
}

void Label::reset()
{
    AX_SAFE_RELEASE_NULL(_textSprite);
    AX_SAFE_RELEASE_NULL(_shadowNode);
    Node::removeAllChildrenWithCleanup(true);
    AX_SAFE_RELEASE_NULL(_reusedLetter);
    _letters.clear();
    _batchNodes.clear();
    _batchCommands.clear();
    _lettersInfo.clear();
    if (_fontAtlas)
    {
        FontAtlasCache::releaseFontAtlas(_fontAtlas);
        _fontAtlas = nullptr;
    }

    _currentLabelType = LabelType::STRING_TEXTURE;
    _currLabelEffect  = LabelEffect::NORMAL;
    _contentDirty     = false;
    _numberOfLines    = 0;
    _lengthOfString   = 0;
    _utf32Text.clear();
    _utf8Text.clear();

    TTFConfig temp;
    _fontConfig  = temp;
    _outlineSize = 0.f;

    _bmFontPath      = "";
    _bmSubTextureKey = "";
    _bmRect          = Rect::ZERO;
    _bmRotated       = false;

    _systemFontDirty = false;
    _systemFont      = "Helvetica";
    _systemFontSize  = AX_DEFAULT_FONT_LABEL_SIZE;

    if (_horizontalKernings)
    {
        delete[] _horizontalKernings;
        _horizontalKernings = nullptr;
    }
    _additionalKerning      = 0.f;
    _lineHeight             = 0.f;
    _lineSpacing            = 0.f;
    _maxLineWidth           = 0.f;
    _labelDimensions.width  = 0.f;
    _labelDimensions.height = 0.f;
    _labelWidth             = 0.f;
    _labelHeight            = 0.f;
    _lineBreakWithoutSpaces = false;
    _hAlignment             = TextHAlignment::LEFT;
    _vAlignment             = TextVAlignment::TOP;

    _effectColorF = Color4F::BLACK;
    _textColor    = Color4B::WHITE;
    _textColorF   = Color4F::WHITE;
    setColor(Color3B::WHITE);

    _shadowDirty      = false;
    _shadowEnabled    = false;
    _shadowBlurRadius = 0.f;

    _uniformEffectColor = -1;
    _uniformEffectType  = -1;
    _uniformTextColor   = -1;

    _useDistanceField   = false;
    _useA8Shader        = false;
    _clipEnabled        = false;
    _blendFuncDirty     = false;
    _blendFunc          = BlendFunc::ALPHA_PREMULTIPLIED;
    _isOpacityModifyRGB = false;
    _insideBounds       = true;
    _enableWrap         = true;
    _bmFontSize         = -1;
    _bmfontScale        = 1.0f;
    _overflow           = Overflow::NONE;
    _originalFontSize   = 0.0f;
    _boldEnabled        = false;
    if (_underlineNode)
    {
        removeChild(_underlineNode);
        _underlineNode = nullptr;
    }
    _strikethroughEnabled = false;
    setRotationSkewX(0);  // reverse italics
}

//  ETC1 ALPHA supports, for LabelType::BMFONT & LabelType::CHARMAP
static Texture2D* _getTexture(Label* label)
{
    auto fontAtlas     = label->getFontAtlas();
    Texture2D* texture = nullptr;
    if (fontAtlas != nullptr)
    {
        auto textures = fontAtlas->getTextures();
        if (!textures.empty())
        {
            texture = textures.begin()->second;
        }
    }
    return texture;
}

void Label::setVertexLayout()
{
    _programState->validateSharedVertexLayout(backend::VertexLayoutType::Sprite);
}

bool Label::setProgramState(backend::ProgramState* programState, bool ownPS /*= false*/)
{
    if (Node::setProgramState(programState, ownPS))
    {
        updateUniformLocations();
        for (auto&& batch : _batchCommands)
        {
            updateBatchCommand(batch);
        }

        setVertexLayout();

        auto& quadPipeline        = _quadCommand.getPipelineDescriptor();
        quadPipeline.programState = _programState;

        return true;
    }
    return false;
}

void Label::updateShaderProgram()
{
    uint32_t programType = backend::ProgramType::POSITION_TEXTURE_COLOR;
    if (_currentLabelType == LabelType::BMFONT || _currentLabelType == LabelType::CHARMAP)
    {
        auto texture = _getTexture(this);
        if (texture)
        {
            programType =
                backend::ProgramStateRegistry::getInstance()->getProgramType(programType, texture->getSamplerFlags());
        }
    }
    else
    {
        switch (_currLabelEffect)
        {
        case ax::LabelEffect::NORMAL:
            if (_useDistanceField)
                programType = backend::ProgramType::LABEL_DISTANCE_NORMAL;
            else if (_useA8Shader)
                programType = backend::ProgramType::LABEL_NORMAL;
            else
            {
                auto texture = _getTexture(this);
                if (texture)
                {
                    programType = backend::ProgramStateRegistry::getInstance()->getProgramType(
                        programType, texture->getSamplerFlags());
                }
            }
            break;
        case ax::LabelEffect::OUTLINE:
            programType =
                _useDistanceField ? backend::ProgramType::LABEL_DISTANCE_OUTLINE : backend::ProgramType::LABLE_OUTLINE;
            break;
        case ax::LabelEffect::GLOW:
            if (_useDistanceField)
                programType = backend::ProgramType::LABLE_DISTANCE_GLOW;
            break;
        default:
            return;
        }
    }

    this->setProgramStateByProgramId(programType);

    updateUniformLocations();

    for (auto&& batch : _batchCommands)
        updateBatchCommand(batch);

    auto& quadPipeline        = _quadCommand.getPipelineDescriptor();
    quadPipeline.programState = _programState;
}

void Label::updateBatchCommand(Label::BatchCommand& batch)
{
    AXASSERT(_programState, "programState should be set!");
    batch.setProgramState(_programState);
}

void Label::updateUniformLocations()
{
    _mvpMatrixLocation   = _programState->getUniformLocation(backend::Uniform::MVP_MATRIX);
    _textureLocation     = _programState->getUniformLocation(backend::Uniform::TEXTURE);
    _textColorLocation   = _programState->getUniformLocation(backend::Uniform::TEXT_COLOR);
    _effectColorLocation = _programState->getUniformLocation(backend::Uniform::EFFECT_COLOR);
    _effectTypeLocation  = _programState->getUniformLocation(backend::Uniform::EFFECT_TYPE);
}

void Label::setFontAtlas(FontAtlas* atlas, bool distanceFieldEnabled /* = false */, bool useA8Shader /* = false */)
{
    if (atlas)
    {
        _systemFontDirty = false;
    }

    if (atlas == _fontAtlas)
        return;

    AX_SAFE_RETAIN(atlas);
    if (_fontAtlas)
    {
        _batchNodes.clear();
        FontAtlasCache::releaseFontAtlas(_fontAtlas);
    }
    _fontAtlas = atlas;

    if (_reusedLetter == nullptr)
    {
        _reusedLetter = Sprite::create();
        _reusedLetter->setOpacityModifyRGB(_isOpacityModifyRGB);
        _reusedLetter->retain();
        _reusedLetter->setAnchorPoint(Vec2::ANCHOR_TOP_LEFT);
    }

    if (_fontAtlas)
    {
        _lineHeight      = _fontAtlas->getLineHeight();
        _contentDirty    = true;
        _systemFontDirty = false;
    }
    _useDistanceField = distanceFieldEnabled;
    _useA8Shader      = useA8Shader;

    if (_currentLabelType != LabelType::TTF)
    {
        _currLabelEffect = LabelEffect::NORMAL;
        updateShaderProgram();
    }
}

bool Label::setTTFConfig(const TTFConfig& ttfConfig)
{
    _originalFontSize = ttfConfig.fontSize;
    return setTTFConfigInternal(ttfConfig);
}

bool Label::setBMFontFilePath(std::string_view bmfontFilePath, float fontSize)
{
    FontAtlas* newAtlas = FontAtlasCache::getFontAtlasFNT(bmfontFilePath);

    if (!newAtlas)
    {
        reset();
        return false;
    }

    // assign the default fontSize
    if (std::abs(fontSize) < FLT_EPSILON)
    {
        FontFNT* bmFont = (FontFNT*)newAtlas->getFont();
        if (bmFont)
        {
            float originalFontSize = bmFont->getOriginalFontSize();
            _bmFontSize            = originalFontSize / AX_CONTENT_SCALE_FACTOR();
        }
    }

    if (fontSize > 0.0f)
    {
        _bmFontSize = fontSize;
    }

    _bmFontPath = bmfontFilePath;

    _currentLabelType = LabelType::BMFONT;
    setFontAtlas(newAtlas);

    return true;
}

bool Label::setBMFontFilePath(std::string_view bmfontFilePath, const Rect& imageRect, bool imageRotated, float fontSize)
{
    FontAtlas* newAtlas = FontAtlasCache::getFontAtlasFNT(bmfontFilePath, imageRect, imageRotated);

    if (!newAtlas)
    {
        reset();
        return false;
    }

    // assign the default fontSize
    if (std::abs(fontSize) < FLT_EPSILON)
    {
        FontFNT* bmFont = (FontFNT*)newAtlas->getFont();
        if (bmFont)
        {
            float originalFontSize = bmFont->getOriginalFontSize();
            _bmFontSize            = originalFontSize / AX_CONTENT_SCALE_FACTOR();
        }
    }

    if (fontSize > 0.0f)
    {
        _bmFontSize = fontSize;
    }

    _bmFontPath = bmfontFilePath;
    _bmRect     = imageRect;
    _bmRotated  = imageRotated;

    _currentLabelType = LabelType::BMFONT;
    setFontAtlas(newAtlas);

    return true;
}

bool Label::setBMFontFilePath(std::string_view bmfontFilePath, std::string_view subTextureKey, float fontSize)
{
    FontAtlas* newAtlas = FontAtlasCache::getFontAtlasFNT(bmfontFilePath, subTextureKey);

    if (!newAtlas)
    {
        reset();
        return false;
    }

    // assign the default fontSize
    if (std::abs(fontSize) < FLT_EPSILON)
    {
        FontFNT* bmFont = (FontFNT*)newAtlas->getFont();
        if (bmFont)
        {
            float originalFontSize = bmFont->getOriginalFontSize();
            _bmFontSize            = originalFontSize / AX_CONTENT_SCALE_FACTOR();
        }
    }

    if (fontSize > 0.0f)
    {
        _bmFontSize = fontSize;
    }

    _bmFontPath      = bmfontFilePath;
    _bmSubTextureKey = subTextureKey;

    _currentLabelType = LabelType::BMFONT;
    setFontAtlas(newAtlas);

    return true;
}

bool Label::setBMFontFilePath(std::string_view bmfontFilePath, const Vec2& imageOffset, float fontSize)
{
    return setBMFontFilePath(bmfontFilePath, Rect(imageOffset.x, imageOffset.y, 0, 0), false);
}

void Label::setString(std::string_view text)
{
    if (text.compare(_utf8Text))
    {
        _utf8Text     = text;
        _contentDirty = true;

        std::u32string utf32String;
        if (StringUtils::UTF8ToUTF32(_utf8Text, utf32String))
        {
            _utf32Text = utf32String;
        }
    }
}

void Label::setAlignment(TextHAlignment hAlignment, TextVAlignment vAlignment)
{
    if (hAlignment != _hAlignment || vAlignment != _vAlignment)
    {
        _hAlignment = hAlignment;
        _vAlignment = vAlignment;

        _contentDirty = true;
    }
}

void Label::setMaxLineWidth(float maxLineWidth)
{
    if (_labelWidth == 0 && _maxLineWidth != maxLineWidth)
    {
        _maxLineWidth = maxLineWidth;
        _contentDirty = true;
    }
}

void Label::setDimensions(float width, float height)
{
    if (_overflow == Overflow::RESIZE_HEIGHT)
    {
        height = 0;
    }
    if (height != _labelHeight || width != _labelWidth)
    {
        _labelWidth             = width;
        _labelHeight            = height;
        _labelDimensions.width  = width;
        _labelDimensions.height = height;

        _maxLineWidth = width;
        _contentDirty = true;

        if (_overflow == Overflow::SHRINK)
        {
            if (_originalFontSize > 0)
            {
                this->restoreFontSize();
            }
        }
    }
}

void Label::restoreFontSize()
{
    if (_currentLabelType == LabelType::TTF)
    {
        auto ttfConfig     = this->getTTFConfig();
        ttfConfig.fontSize = _originalFontSize;
        this->setTTFConfigInternal(ttfConfig);
    }
    else if (_currentLabelType == LabelType::BMFONT)
    {
        this->setBMFontSizeInternal(_originalFontSize);
    }
    else if (_currentLabelType == LabelType::STRING_TEXTURE)
    {
        this->setSystemFontSize(_originalFontSize);
    }
}

void Label::setLineBreakWithoutSpace(bool breakWithoutSpace)
{
    if (breakWithoutSpace != _lineBreakWithoutSpaces)
    {
        _lineBreakWithoutSpaces = breakWithoutSpace;
        _contentDirty           = true;
    }
}

void Label::updateLabelLetters()
{
    if (!_letters.empty())
    {
        Rect uvRect;
        LabelLetter* letterSprite;
        int letterIndex;

        for (auto it = _letters.begin(); it != _letters.end();)
        {
            letterIndex  = it->first;
            letterSprite = (LabelLetter*)it->second;

            if (letterIndex >= _lengthOfString)
            {
                Node::removeChild(letterSprite, true);
                it = _letters.erase(it);
            }
            else
            {
                auto& letterInfo = _lettersInfo[letterIndex];
                if (letterInfo.valid)
                {
                    auto& letterDef    = _fontAtlas->_letterDefinitions[letterInfo.utf32Char];
                    uvRect.size.height = letterDef.height;
                    uvRect.size.width  = letterDef.width;
                    uvRect.origin.x    = letterDef.U;
                    uvRect.origin.y    = letterDef.V;

                    auto batchNode = _batchNodes.at(letterDef.textureID);
                    letterSprite->setTextureAtlas(batchNode->getTextureAtlas());
                    letterSprite->setTexture(_fontAtlas->getTexture(letterDef.textureID));
                    if (letterDef.width <= 0.f || letterDef.height <= 0.f)
                    {
                        letterSprite->setTextureAtlas(nullptr);
                    }
                    else
                    {
                        letterSprite->setTextureRect(uvRect, letterDef.rotated, uvRect.size);
                        letterSprite->setTextureAtlas(_batchNodes.at(letterDef.textureID)->getTextureAtlas());
                        letterSprite->setAtlasIndex(_lettersInfo[letterIndex].atlasIndex);
                    }

                    auto px = letterInfo.positionX + letterDef.width / 2 + _linesOffsetX[letterInfo.lineIndex];
                    auto py = letterInfo.positionY - letterDef.height / 2 + _letterOffsetY;
                    letterSprite->setPosition(px, py);
                }
                else
                {
                    letterSprite->setTextureAtlas(nullptr);
                }
                this->updateLetterSpriteScale(letterSprite);
                ++it;
            }
        }
    }
}

bool Label::alignText()
{
    if (_fontAtlas == nullptr || _utf32Text.empty())
    {
        setContentSize(Vec2::ZERO);
        return true;
    }

    bool ret = true;
    do
    {
        _fontAtlas->prepareLetterDefinitions(_utf32Text);
        auto& textures = _fontAtlas->getTextures();
        auto size      = textures.size();
        if (size > static_cast<size_t>(_batchNodes.size()))
        {
            for (auto index = static_cast<unsigned int>(_batchNodes.size()); index < size; ++index)
            {
                auto batchNode = SpriteBatchNode::createWithTexture(textures.at(index));
                if (batchNode)
                {
                    _isOpacityModifyRGB = batchNode->getTexture()->hasPremultipliedAlpha();
                    _blendFunc          = batchNode->getBlendFunc();
                    batchNode->setAnchorPoint(Vec2::ANCHOR_TOP_LEFT);
                    batchNode->setPosition(Vec2::ZERO);
                    _batchNodes.pushBack(batchNode);
                }
            }
        }
        if (_batchNodes.empty())
        {
            return true;
        }
        // optimize for one-texture-only scenario
        // if multiple textures, then we should count how many chars
        // are per texture
        if (_batchNodes.size() == 1)
            _batchNodes.at(0)->reserveCapacity(_utf32Text.size());

        _reusedLetter->setBatchNode(_batchNodes.at(0));

        _lengthOfString    = 0;
        _textDesiredHeight = 0.f;
        _linesWidth.clear();
        if (_maxLineWidth > 0.f && !_lineBreakWithoutSpaces)
        {
            multilineTextWrapByWord();
        }
        else
        {
            multilineTextWrapByChar();
        }
        computeAlignmentOffset();

        if (_overflow == Overflow::SHRINK)
        {
            float fontSize = this->getRenderingFontSize();

            if (fontSize > 0 && isVerticalClamp())
            {
                this->shrinkLabelToContentSize(AX_CALLBACK_0(Label::isVerticalClamp, this));
            }
        }

        if (!updateQuads())
        {
            ret = false;
            if (_overflow == Overflow::SHRINK)
            {
                this->shrinkLabelToContentSize(AX_CALLBACK_0(Label::isHorizontalClamp, this));
            }
            break;
        }

        updateLabelLetters();

        updateColor();
    } while (0);

    return ret;
}

bool Label::computeHorizontalKernings(const std::u32string& stringToRender)
{
    if (_horizontalKernings)
    {
        delete[] _horizontalKernings;
        _horizontalKernings = nullptr;
    }

    int letterCount     = 0;
    _horizontalKernings = _fontAtlas->getFont()->getHorizontalKerningForTextUTF32(stringToRender, letterCount);

    if (!_horizontalKernings)
        return false;
    else
        return true;
}

bool Label::isHorizontalClamped(float letterPositionX, int lineIndex)
{
    auto wordWidth       = this->_linesWidth[lineIndex];
    bool letterOverClamp = (letterPositionX > _contentSize.width || letterPositionX < 0);
    if (!_enableWrap)
    {
        return letterOverClamp;
    }
    else
    {
        return (wordWidth > this->_contentSize.width && letterOverClamp);
    }
}

bool Label::updateQuads()
{
    bool ret = true;
    for (auto&& batchNode : _batchNodes)
    {
        batchNode->getTextureAtlas()->removeAllQuads();
    }

    for (int ctr = 0; ctr < _lengthOfString; ++ctr)
    {
        if (_lettersInfo[ctr].valid)
        {
            auto& letterDef = _fontAtlas->_letterDefinitions[_lettersInfo[ctr].utf32Char];

            _reusedRect.size.height = letterDef.height;
            _reusedRect.size.width  = letterDef.width;
            _reusedRect.origin.x    = letterDef.U;
            _reusedRect.origin.y    = letterDef.V;

            auto py = _lettersInfo[ctr].positionY + _letterOffsetY;
            if (_labelHeight > 0.f)
            {
                if (py > _tailoredTopY)
                {
                    auto clipTop = py - _tailoredTopY;
                    _reusedRect.origin.y += clipTop;
                    _reusedRect.size.height -= clipTop;
                    py -= clipTop;
                }
                if (py - letterDef.height * _bmfontScale < _tailoredBottomY)
                {
                    _reusedRect.size.height = (py < _tailoredBottomY) ? 0.f : (py - _tailoredBottomY);
                }
            }

            auto lineIndex = _lettersInfo[ctr].lineIndex;
            auto px = _lettersInfo[ctr].positionX + letterDef.width / 2 * _bmfontScale + _linesOffsetX[lineIndex];

            if (_labelWidth > 0.f)
            {
                if (this->isHorizontalClamped(px, lineIndex))
                {
                    if (_overflow == Overflow::CLAMP)
                    {
                        _reusedRect.size.width = 0;
                    }
                    else if (_overflow == Overflow::SHRINK)
                    {
                        if (_contentSize.width > letterDef.width)
                        {
                            ret = false;
                            break;
                        }
                        else
                        {
                            _reusedRect.size.width = 0;
                        }
                    }
                }
            }

            if (_reusedRect.size.height > 0.f && _reusedRect.size.width > 0.f)
            {
                _reusedLetter->setTextureRect(_reusedRect, letterDef.rotated, _reusedRect.size);
                float letterPositionX = _lettersInfo[ctr].positionX + _linesOffsetX[_lettersInfo[ctr].lineIndex];
                _reusedLetter->setPosition(letterPositionX, py);
                auto index = static_cast<int>(_batchNodes.at(letterDef.textureID)->getTextureAtlas()->getTotalQuads());
                _lettersInfo[ctr].atlasIndex = index;

                this->updateLetterSpriteScale(_reusedLetter);

                _batchNodes.at(letterDef.textureID)->insertQuadFromSprite(_reusedLetter, index);
            }
        }
    }

    return ret;
}

bool Label::setTTFConfigInternal(const TTFConfig& ttfConfig)
{
    FontAtlas* newAtlas = FontAtlasCache::getFontAtlasTTF(&ttfConfig);

    if (!newAtlas)
    {
        reset();
        return false;
    }

    _currentLabelType = LabelType::TTF;
    setFontAtlas(newAtlas, ttfConfig.distanceFieldEnabled, true);

    _fontConfig = ttfConfig;

    if (_fontConfig.outlineSize > 0)
    {
        _useA8Shader     = false;
        _currLabelEffect = LabelEffect::OUTLINE;
        updateShaderProgram();
    }
    else
    {
        _currLabelEffect = LabelEffect::NORMAL;
        updateShaderProgram();
    }

    if (_fontConfig.italics)
        this->enableItalics();
    if (_fontConfig.bold)
        this->enableBold();
    if (_fontConfig.underline)
        this->enableUnderline();
    if (_fontConfig.strikethrough)
        this->enableStrikethrough();

    return true;
}

void Label::setBMFontSizeInternal(float fontSize)
{
    if (_currentLabelType == LabelType::BMFONT)
    {
        if (!_bmSubTextureKey.empty())
        {
            this->setBMFontFilePath(_bmFontPath, _bmSubTextureKey, fontSize);
        }
        else
        {
            this->setBMFontFilePath(_bmFontPath, _bmRect, _bmRotated, fontSize);
        }
        _contentDirty = true;
    }
}

void Label::scaleFontSize(float fontSize)
{
    bool shouldUpdateContent = true;
    if (_currentLabelType == LabelType::TTF)
    {
        auto ttfConfig     = this->getTTFConfig();
        ttfConfig.fontSize = fontSize;
        this->setTTFConfigInternal(ttfConfig);
    }
    else if (_currentLabelType == LabelType::BMFONT)
    {
        if (std::abs(fontSize) < FLT_EPSILON)
        {
            fontSize            = 0.1f;
            shouldUpdateContent = false;
        }
        this->setBMFontSizeInternal(fontSize);
    }
    else if (_currentLabelType == LabelType::STRING_TEXTURE)
    {
        this->setSystemFontSize(fontSize);
    }

    if (shouldUpdateContent)
    {
        this->updateContent();
    }
}

void Label::enableGlow(const Color4B& glowColor)
{
    if (_currentLabelType == LabelType::TTF)
    {
        if (_fontConfig.distanceFieldEnabled == false)
        {
            auto config                 = _fontConfig;
            config.outlineSize          = 0;
            config.distanceFieldEnabled = true;
            setTTFConfig(config);
            _contentDirty = true;
        }
        _currLabelEffect = LabelEffect::GLOW;
        _effectColorF.r  = glowColor.r / 255.0f;
        _effectColorF.g  = glowColor.g / 255.0f;
        _effectColorF.b  = glowColor.b / 255.0f;
        _effectColorF.a  = glowColor.a / 255.0f;
        updateShaderProgram();
    }
}

void Label::enableOutline(const Color4B& outlineColor, int outlineSize /* = -1 */)
{
    AXASSERT(_currentLabelType == LabelType::STRING_TEXTURE || _currentLabelType == LabelType::TTF,
             "Only supported system font and TTF!");

    if (outlineSize > 0 || _currLabelEffect == LabelEffect::OUTLINE)
    {
        if (_currentLabelType == LabelType::TTF)
        {
            _effectColorF.r = outlineColor.r / 255.0f;
            _effectColorF.g = outlineColor.g / 255.0f;
            _effectColorF.b = outlineColor.b / 255.0f;
            _effectColorF.a = outlineColor.a / 255.0f;

            if (!_useDistanceField)
            {  // not SDF, request font atlas from feetype
                if (outlineSize > 0 && _fontConfig.outlineSize != outlineSize)
                {
                    _fontConfig.outlineSize = outlineSize;
                    setTTFConfig(_fontConfig);
                }
            }
            else
                updateShaderProgram();
        }
        else if (_effectColorF != outlineColor || _outlineSize != outlineSize)
        {
            _effectColorF.r  = outlineColor.r / 255.f;
            _effectColorF.g  = outlineColor.g / 255.f;
            _effectColorF.b  = outlineColor.b / 255.f;
            _effectColorF.a  = outlineColor.a / 255.f;
            _currLabelEffect = LabelEffect::OUTLINE;
            _contentDirty    = true;
        }
        _outlineSize = outlineSize;
    }
}

void Label::enableShadow(const Color4B& shadowColor /* = Color4B::BLACK */,
                         const Vec2& offset /* = Vec2(2 ,-2)*/,
                         int /* blurRadius = 0 */)
{
    _shadowEnabled = true;
    _shadowDirty   = true;

    _shadowOffset.width  = offset.width;
    _shadowOffset.height = offset.height;
    // TODO: support blur for shadow

    _shadowColor3B.r = shadowColor.r;
    _shadowColor3B.g = shadowColor.g;
    _shadowColor3B.b = shadowColor.b;
    _shadowOpacity   = shadowColor.a;

    if (!_systemFontDirty && !_contentDirty && _textSprite)
    {
        auto fontDef = _getFontDefinition();
        if (_shadowNode)
        {
            if (shadowColor != _shadowColor4F)
            {
                _shadowNode->release();
                _shadowNode = nullptr;
                createShadowSpriteForSystemFont(fontDef);
            }
            else
            {
                _shadowNode->setPosition(_shadowOffset.width, _shadowOffset.height);
            }
        }
        else
        {
            createShadowSpriteForSystemFont(fontDef);
        }
    }

    _shadowColor4F.r = shadowColor.r / 255.0f;
    _shadowColor4F.g = shadowColor.g / 255.0f;
    _shadowColor4F.b = shadowColor.b / 255.0f;
    _shadowColor4F.a = shadowColor.a / 255.0f;

    if (_currentLabelType == LabelType::BMFONT || _currentLabelType == LabelType::CHARMAP)
    {
        updateShaderProgram();
    }
}

void Label::enableItalics()
{
    setRotationSkewX(12);
}

void Label::enableBold()
{
    if (!_boldEnabled)
    {
        // bold is implemented with outline
        enableShadow(Color4B::WHITE, Vec2(0.9f, 0), 0);
        // add one to kerning
        setAdditionalKerning(_additionalKerning + 1);
        _boldEnabled = true;
    }
}

void Label::enableUnderline()
{
    // remove it, just in case to prevent adding two or more
    if (!_underlineNode)
    {
        _underlineNode = DrawNode::create();
        _underlineNode->setGlobalZOrder(getGlobalZOrder());
        addChild(_underlineNode, 100000);
        _contentDirty = true;
    }
}

void Label::enableStrikethrough()
{
    if (!_strikethroughEnabled)
    {
        enableUnderline();
        _strikethroughEnabled = true;
    }
}

void Label::disableEffect()
{
    disableEffect(LabelEffect::ALL);
}

void Label::disableEffect(LabelEffect effect)
{
    switch (effect)
    {
    case ax::LabelEffect::NORMAL:
        break;
    case ax::LabelEffect::OUTLINE:
        if (_currLabelEffect == LabelEffect::OUTLINE)
        {
            if (_currentLabelType == LabelType::TTF)
            {
                _fontConfig.outlineSize = 0;
                setTTFConfig(_fontConfig);
            }
            _currLabelEffect = LabelEffect::NORMAL;
            _contentDirty    = true;
        }
        break;
    case ax::LabelEffect::SHADOW:
        if (_shadowEnabled)
        {
            _shadowEnabled = false;
            AX_SAFE_RELEASE_NULL(_shadowNode);
            updateShaderProgram();
        }
        break;
    case ax::LabelEffect::GLOW:
        if (_currLabelEffect == LabelEffect::GLOW)
        {
            _currLabelEffect = LabelEffect::NORMAL;
            updateShaderProgram();
        }
        break;
    case ax::LabelEffect::ITALICS:
        setRotationSkewX(0);
        break;
    case ax::LabelEffect::BOLD:
        if (_boldEnabled)
        {
            _boldEnabled = false;
            _additionalKerning -= 1;
            disableEffect(LabelEffect::SHADOW);
        }
        break;
    case ax::LabelEffect::UNDERLINE:
        if (_underlineNode)
        {
            removeChild(_underlineNode);
            _underlineNode = nullptr;
        }
        break;
    case ax::LabelEffect::STRIKETHROUGH:
        _strikethroughEnabled = false;
        // since it is based on underline, disable it as well
        disableEffect(LabelEffect::UNDERLINE);
        break;
    case LabelEffect::ALL:
    {
        disableEffect(LabelEffect::SHADOW);
        disableEffect(LabelEffect::GLOW);
        disableEffect(LabelEffect::OUTLINE);
        disableEffect(LabelEffect::ITALICS);
        disableEffect(LabelEffect::BOLD);
        disableEffect(LabelEffect::UNDERLINE);
        disableEffect(LabelEffect::STRIKETHROUGH);
    }
    break;
    default:
        break;
    }
}

void Label::createSpriteForSystemFont(const FontDefinition& fontDef)
{
    _currentLabelType = LabelType::STRING_TEXTURE;

    auto texture = new Texture2D;
    texture->initWithString(_utf8Text, fontDef);

    _textSprite = Sprite::createWithTexture(texture);
    // set camera mask using label's camera mask, because _textSprite may be null when setting camera mask to label
    _textSprite->setCameraMask(getCameraMask());
    _textSprite->setGlobalZOrder(getGlobalZOrder());
    _textSprite->setAnchorPoint(Vec2::ANCHOR_BOTTOM_LEFT);
    this->setContentSize(_textSprite->getContentSize());
    texture->release();
    if (_blendFuncDirty)
    {
        _textSprite->setBlendFunc(_blendFunc);
    }

    _textSprite->retain();
    _textSprite->updateDisplayedColor(_displayedColor);
    _textSprite->updateDisplayedOpacity(_displayedOpacity);
}

void Label::createShadowSpriteForSystemFont(const FontDefinition& fontDef)
{
    if (!fontDef._stroke._strokeEnabled && fontDef._fontFillColor == _shadowColor3B &&
        (fontDef._fontAlpha == _shadowOpacity))
    {
        _shadowNode = Sprite::createWithTexture(_textSprite->getTexture());
    }
    else
    {
        FontDefinition shadowFontDefinition   = fontDef;
        shadowFontDefinition._fontFillColor.r = _shadowColor3B.r;
        shadowFontDefinition._fontFillColor.g = _shadowColor3B.g;
        shadowFontDefinition._fontFillColor.b = _shadowColor3B.b;
        shadowFontDefinition._fontAlpha       = _shadowOpacity;

        shadowFontDefinition._stroke._strokeColor = shadowFontDefinition._fontFillColor;
        shadowFontDefinition._stroke._strokeAlpha = shadowFontDefinition._fontAlpha;

        auto texture = new Texture2D;
        texture->initWithString(_utf8Text, shadowFontDefinition);
        _shadowNode = Sprite::createWithTexture(texture);
        texture->release();
    }

    if (_shadowNode)
    {
        if (_blendFuncDirty)
        {
            _shadowNode->setBlendFunc(_blendFunc);
        }
        _shadowNode->setCameraMask(getCameraMask());
        _shadowNode->setGlobalZOrder(getGlobalZOrder());
        _shadowNode->setAnchorPoint(Vec2::ANCHOR_BOTTOM_LEFT);
        _shadowNode->setPosition(_shadowOffset.width, _shadowOffset.height);

        _shadowNode->retain();
        _shadowNode->updateDisplayedColor(_displayedColor);
        _shadowNode->updateDisplayedOpacity(_displayedOpacity);
    }
}

void Label::setCameraMask(unsigned short mask, bool applyChildren)
{
    Node::setCameraMask(mask, applyChildren);

    if (_textSprite)
    {
        _textSprite->setCameraMask(mask, applyChildren);
    }
    if (_shadowNode)
    {
        _shadowNode->setCameraMask(mask, applyChildren);
    }
}

void Label::updateContent()
{
    if (_systemFontDirty)
    {
        if (_fontAtlas)
        {
            _batchNodes.clear();
            _batchCommands.clear();
            AX_SAFE_RELEASE_NULL(_reusedLetter);
            FontAtlasCache::releaseFontAtlas(_fontAtlas);
            _fontAtlas = nullptr;
        }

        _systemFontDirty = false;
    }

    AX_SAFE_RELEASE_NULL(_textSprite);
    AX_SAFE_RELEASE_NULL(_shadowNode);
    bool updateFinished = true;

    if (_fontAtlas)
    {
        std::u32string utf32String;
        if (StringUtils::UTF8ToUTF32(_utf8Text, utf32String))
        {
            _utf32Text = utf32String;
        }

        computeHorizontalKernings(_utf32Text);
        updateFinished = alignText();
    }
    else
    {
        auto fontDef = _getFontDefinition();
        createSpriteForSystemFont(fontDef);
        if (_shadowEnabled)
        {
            createShadowSpriteForSystemFont(fontDef);
        }
    }

    if (_underlineNode)
    {
        _underlineNode->clear();

        if (_numberOfLines)
        {
            // This is the logic for TTF fonts
            const float charheight = (_textDesiredHeight / _numberOfLines);
            _underlineNode->setLineWidth(charheight / 6);

            // atlas font
            for (int i = 0; i < _numberOfLines; ++i)
            {
                float offsety = 0;
                if (_strikethroughEnabled)
                    offsety += charheight / 2;
                // FIXME: Might not work with different vertical alignments
                float y = (_numberOfLines - i - 1) * charheight + offsety;

                // Github issue #15214. Uses _displayedColor instead of _textColor for the underline.
                // This is to have the same behavior of SystemFonts.
                _underlineNode->drawLine(Vec2(_linesOffsetX[i], y), Vec2(_linesWidth[i] + _linesOffsetX[i], y),
                                         Color4F(_displayedColor));
            }
        }
        else if (_textSprite)
        {
            // ...and is the logic for System fonts
            float y               = 0;
            const auto spriteSize = _textSprite->getContentSize();
            _underlineNode->setLineWidth(spriteSize.height / 6);

            if (_strikethroughEnabled)
                // FIXME: system fonts don't report the height of the font correctly. only the size of the texture,
                // which is POT
                y += spriteSize.height / 2;
            // FIXME: Might not work with different vertical alignments
            _underlineNode->drawLine(Vec2(0.0f, y), Vec2(spriteSize.width, y),
                                     Color4F(_textSprite->getDisplayedColor()));
        }
    }

    if (updateFinished)
    {
        _contentDirty = false;
    }

#if AX_LABEL_DEBUG_DRAW
    _debugDrawNode->clear();
    Vec2 vertices[4] = {Vec2::ZERO, Vec2(_contentSize.width, 0.0f), Vec2(_contentSize.width, _contentSize.height),
                        Vec2(0.0f, _contentSize.height)};
    _debugDrawNode->drawPoly(vertices, 4, true, Color4F::WHITE);
#endif
}

void Label::setBMFontSize(float fontSize)
{
    this->setBMFontSizeInternal(fontSize);
    _originalFontSize = fontSize;
}

float Label::getBMFontSize() const
{
    return _bmFontSize;
}

void Label::updateBuffer(TextureAtlas* textureAtlas, CustomCommand& customCommand)
{
    if (textureAtlas->getTotalQuads() > customCommand.getVertexCapacity())
    {
        customCommand.createVertexBuffer((unsigned int)sizeof(V3F_C4B_T2F_Quad),
                                         (unsigned int)textureAtlas->getTotalQuads(),
                                         CustomCommand::BufferUsage::DYNAMIC);
        customCommand.createIndexBuffer(CustomCommand::IndexFormat::U_SHORT,
                                        (unsigned int)textureAtlas->getTotalQuads() * 6,
                                        CustomCommand::BufferUsage::DYNAMIC);
    }
    customCommand.updateVertexBuffer(textureAtlas->getQuads(),
                                     (unsigned int)(textureAtlas->getTotalQuads() * sizeof(V3F_C4B_T2F_Quad)));
    customCommand.updateIndexBuffer(textureAtlas->getIndices(),
                                    (unsigned int)(textureAtlas->getTotalQuads() * 6 * sizeof(unsigned short)));
    customCommand.setIndexDrawInfo(0, (unsigned int)(textureAtlas->getTotalQuads() * 6));
}

void Label::updateEffectUniforms(BatchCommand& batch,
                                 TextureAtlas* textureAtlas,
                                 Renderer* renderer,
                                 const Mat4& transform)
{
    updateBuffer(textureAtlas, batch.textCommand);

    auto& matrixProjection = _director->getMatrix(MATRIX_STACK_TYPE::MATRIX_STACK_PROJECTION);

    if (_shadowEnabled)
    {
        updateBuffer(textureAtlas, batch.shadowCommand);
        auto shadowMatrix = matrixProjection * _shadowTransform;
        batch.shadowCommand.getPipelineDescriptor().programState->setUniform(_mvpMatrixLocation, shadowMatrix.m,
                                                                             sizeof(shadowMatrix.m));
    }

    if (_currentLabelType == LabelType::TTF)
    {
        switch (_currLabelEffect)
        {
        case LabelEffect::OUTLINE:
        {
            int effectType = 0;
            Vec4 effectColor(_effectColorF.r, _effectColorF.g, _effectColorF.b, _effectColorF.a);
            // draw shadow
            if (_shadowEnabled)
            {
                effectType               = 2;
                Vec4 shadowColor         = Vec4(_shadowColor4F.r, _shadowColor4F.g, _shadowColor4F.b, _shadowColor4F.a);
                auto* programStateShadow = batch.shadowCommand.getPipelineDescriptor().programState;
                programStateShadow->setUniform(_effectColorLocation, &shadowColor, sizeof(Vec4));
                programStateShadow->setUniform(_effectTypeLocation, &effectType, sizeof(effectType));
                batch.shadowCommand.init(_globalZOrder);
                renderer->addCommand(&batch.shadowCommand);
            }

            if (_useDistanceField)
            {  // distance outline
                effectColor.w = _outlineSize > 0 ? _outlineSize : _fontConfig.outlineSize;
                batch.textCommand.getPipelineDescriptor().programState->setUniform(_effectColorLocation, &effectColor,
                                                                                   sizeof(Vec4));
            }
            else
            {
                // draw outline
                {
                    effectType = 1;
                    updateBuffer(textureAtlas, batch.outLineCommand);
                    auto* programStateOutline = batch.outLineCommand.getPipelineDescriptor().programState;
                    programStateOutline->setUniform(_effectColorLocation, &effectColor, sizeof(Vec4));
                    programStateOutline->setUniform(_effectTypeLocation, &effectType, sizeof(effectType));
                    batch.outLineCommand.init(_globalZOrder);
                    renderer->addCommand(&batch.outLineCommand);
                }

                // draw text
                {
                    effectType             = 0;
                    auto* programStateText = batch.textCommand.getPipelineDescriptor().programState;

                    programStateText->setUniform(_effectColorLocation, &effectColor, sizeof(effectColor));
                    programStateText->setUniform(_effectTypeLocation, &effectType, sizeof(effectType));
                }
            }
        }
        break;
        case LabelEffect::NORMAL:
        {
            if (_shadowEnabled)
            {
                Vec4 shadowColor         = Vec4(_shadowColor4F.r, _shadowColor4F.g, _shadowColor4F.b, _shadowColor4F.a);
                auto* programStateShadow = batch.shadowCommand.getPipelineDescriptor().programState;
                programStateShadow->setUniform(_textColorLocation, &shadowColor, sizeof(Vec4));
                batch.shadowCommand.init(_globalZOrder);
                renderer->addCommand(&batch.shadowCommand);
            }
        }
        break;
        case LabelEffect::GLOW:
        {
            // draw shadow
            if (_shadowEnabled)
            {
                Vec4 shadowColor         = Vec4(_shadowColor4F.r, _shadowColor4F.g, _shadowColor4F.b, _shadowColor4F.a);
                auto* programStateShadow = batch.shadowCommand.getPipelineDescriptor().programState;
                programStateShadow->setUniform(_textColorLocation, &shadowColor, sizeof(Vec4));
                programStateShadow->setUniform(_effectColorLocation, &shadowColor, sizeof(Vec4));
                batch.shadowCommand.init(_globalZOrder);
                renderer->addCommand(&batch.shadowCommand);
            }

            Vec4 effectColor(_effectColorF.r, _effectColorF.g, _effectColorF.b, _effectColorF.a);
            batch.textCommand.getPipelineDescriptor().programState->setUniform(_effectColorLocation, &effectColor,
                                                                               sizeof(Vec4));
        }
        break;
        default:
            break;
        }
    }
    else
    {
        if (_shadowEnabled)
        {
            Color3B oldColor   = _realColor;
            uint8_t oldOPacity = _displayedOpacity;
            _displayedOpacity  = _shadowColor4F.a * (oldOPacity / 255.0f) * 255;
            setColor(Color3B(_shadowColor4F));
            batch.shadowCommand.updateVertexBuffer(
                textureAtlas->getQuads(), (unsigned int)(textureAtlas->getTotalQuads() * sizeof(V3F_C4B_T2F_Quad)));
            batch.shadowCommand.init(_globalZOrder);
            renderer->addCommand(&batch.shadowCommand);

            _displayedOpacity = oldOPacity;
            setColor(oldColor);
        }
    }

    batch.textCommand.init(_globalZOrder);
    renderer->addCommand(&batch.textCommand);
}

void Label::draw(Renderer* renderer, const Mat4& transform, uint32_t flags)
{
    if (_batchNodes.empty() || _lengthOfString <= 0)
    {
        return;
    }
    // Don't do calculate the culling if the transform was not updated
    bool transformUpdated = flags & FLAGS_TRANSFORM_DIRTY;
#if AX_USE_CULLING
    auto visitingCamera = Camera::getVisitingCamera();
    auto defaultCamera  = Camera::getDefaultCamera();
    if (visitingCamera == defaultCamera)
    {
        _insideBounds = (transformUpdated || visitingCamera->isViewProjectionUpdated())
                            ? renderer->checkVisibility(transform, _contentSize)
                            : _insideBounds;
    }
    else
    {
        _insideBounds = renderer->checkVisibility(transform, _contentSize);
    }

    if (_insideBounds)
#endif
    {
        ax::Mat4 matrixProjection = _director->getMatrix(MATRIX_STACK_TYPE::MATRIX_STACK_PROJECTION);
        if (!_shadowEnabled && (_currentLabelType == LabelType::BMFONT || _currentLabelType == LabelType::CHARMAP))
        {
            updateBlendState();
            for (auto&& it : _letters)
            {
                it.second->updateTransform();
            }
            // ETC1 ALPHA supports for BMFONT & CHARMAP
            auto textureAtlas = _batchNodes.at(0)->getTextureAtlas();
            if (!textureAtlas->getTotalQuads())
                return;

            auto texture       = textureAtlas->getTexture();
            auto& pipelineQuad = _quadCommand.getPipelineDescriptor();
            pipelineQuad.programState->setUniform(_mvpMatrixLocation, matrixProjection.m, sizeof(matrixProjection.m));
            pipelineQuad.programState->setTexture(texture->getBackendTexture());
            _quadCommand.init(_globalZOrder, texture, _blendFunc, textureAtlas->getQuads(),
                              textureAtlas->getTotalQuads(), transform, flags);
            renderer->addCommand(&_quadCommand);
        }
        else
        {
            ax::Mat4 matrixMVP = matrixProjection * transform;

            for (auto&& it : _letters)
            {
                it.second->updateTransform();
            }
            int i = 0;

            if (_batchCommands.size() != _batchNodes.size())
            {
                _batchCommands.resize(_batchNodes.size());
                updateShaderProgram();
            }

            updateBlendState();

            for (auto&& batchNode : _batchNodes)
            {
                auto textureAtlas = batchNode->getTextureAtlas();
                if (!textureAtlas->getTotalQuads())
                    continue;

                auto& batch = _batchCommands[i++];
                for (auto&& command : batch.getCommandArray())
                {
                    auto* programState = command->getPipelineDescriptor().programState;
                    Vec4 textColor(_textColorF.r, _textColorF.g, _textColorF.b, _textColorF.a);
                    programState->setUniform(_textColorLocation, &textColor, sizeof(Vec4));
                    programState->setTexture(textureAtlas->getTexture()->getBackendTexture());
                }
                batch.textCommand.getPipelineDescriptor().programState->setUniform(_mvpMatrixLocation, matrixMVP.m,
                                                                                   sizeof(matrixMVP.m));
                batch.outLineCommand.getPipelineDescriptor().programState->setUniform(_mvpMatrixLocation, matrixMVP.m,
                                                                                      sizeof(matrixMVP.m));
                updateEffectUniforms(batch, textureAtlas, renderer, transform);
            }
        }
    }
}

void Label::updateBlendState()
{
    setOpacityModifyRGB(_blendFunc != BlendFunc::ALPHA_NON_PREMULTIPLIED);
    for (auto&& batch : _batchCommands)
    {
        for (auto&& command : batch.getCommandArray())
        {
            auto& blendDescriptor = command->getPipelineDescriptor().blendDescriptor;
            updateBlend(blendDescriptor, _blendFunc);
        }
    }
    updateBlend(_quadCommand.getPipelineDescriptor().blendDescriptor, _blendFunc);
}

void Label::visit(Renderer* renderer, const Mat4& parentTransform, uint32_t parentFlags)
{
    if (!_visible || (_utf8Text.empty() && _children.empty()))
    {
        return;
    }

    if (_systemFontDirty || _contentDirty)
    {
        // Label overflow shrink fix #566
        if (_overflow == Overflow::SHRINK && this->getRenderingFontSize() < _originalFontSize)
            rescaleWithOriginalFontSize();

        updateContent();
    }

    uint32_t flags = processParentFlags(parentTransform, parentFlags);

    if (!_utf8Text.empty() && _shadowEnabled && (_shadowDirty || (flags & FLAGS_DIRTY_MASK)))
    {
        _position.x += _shadowOffset.width;
        _position.y += _shadowOffset.height;
        _transformDirty = _inverseDirty = true;

        _shadowTransform = transform(parentTransform);

        _position.x -= _shadowOffset.width;
        _position.y -= _shadowOffset.height;
        _transformDirty = _inverseDirty = true;

        _shadowDirty = false;
    }

    bool visibleByCamera = isVisitableByVisitingCamera();
    if (_children.empty() && !_textSprite && !visibleByCamera)
    {
        return;
    }

    // IMPORTANT:
    // To ease the migration to v3.0, we still support the Mat4 stack,
    // but it is deprecated and your code should not rely on it
    _director->pushMatrix(MATRIX_STACK_TYPE::MATRIX_STACK_MODELVIEW);
    _director->loadMatrix(MATRIX_STACK_TYPE::MATRIX_STACK_MODELVIEW, _modelViewTransform);

    if (!_children.empty())
    {
        sortAllChildren();

        int i = 0;
        // draw children zOrder < 0
        for (auto size = _children.size(); i < size; ++i)
        {
            auto node = _children.at(i);

            if (node && node->getLocalZOrder() < 0)
                node->visit(renderer, _modelViewTransform, flags);
            else
                break;
        }

        this->drawSelf(visibleByCamera, renderer, flags);

        for (auto it = _children.cbegin() + i, itCend = _children.cend(); it != itCend; ++it)
        {
            (*it)->visit(renderer, _modelViewTransform, flags);
        }
    }
    else
    {
        this->drawSelf(visibleByCamera, renderer, flags);
    }

    _director->popMatrix(MATRIX_STACK_TYPE::MATRIX_STACK_MODELVIEW);
}

void Label::drawSelf(bool visibleByCamera, Renderer* renderer, uint32_t flags)
{
    if (_textSprite)
    {
        if (_shadowNode)
        {
            _shadowNode->visit(renderer, _modelViewTransform, flags);
        }
        _textSprite->visit(renderer, _modelViewTransform, flags);
    }
    else if (visibleByCamera && !_utf8Text.empty())
    {
        draw(renderer, _modelViewTransform, flags);
    }
}

void Label::setSystemFontName(std::string_view systemFont)
{
    if (systemFont != _systemFont)
    {
        _systemFont       = systemFont;
        _currentLabelType = LabelType::STRING_TEXTURE;
        _systemFontDirty  = true;
    }
}

void Label::setSystemFontSize(float fontSize)
{
    if (_systemFontSize != fontSize)
    {
        _systemFontSize   = fontSize;
        _originalFontSize = fontSize;
        _currentLabelType = LabelType::STRING_TEXTURE;
        _systemFontDirty  = true;
    }
}

///// PROTOCOL STUFF
Sprite* Label::getLetter(int letterIndex)
{
    Sprite* letter = nullptr;
    do
    {
        if (_systemFontDirty || _currentLabelType == LabelType::STRING_TEXTURE)
        {
            break;
        }

        auto contentDirty = _contentDirty;
        if (contentDirty)
        {
            updateContent();
        }

        if (_textSprite == nullptr && letterIndex < _lengthOfString)
        {
            const auto& letterInfo = _lettersInfo[letterIndex];
            if (!letterInfo.valid || letterInfo.atlasIndex < 0)
            {
                break;
            }

            if (_letters.find(letterIndex) != _letters.end())
            {
                letter = _letters[letterIndex];
            }

            if (letter == nullptr)
            {
                auto& letterDef = _fontAtlas->_letterDefinitions[letterInfo.utf32Char];
                auto textureID  = letterDef.textureID;
                Rect uvRect;
                uvRect.size.height = letterDef.height;
                uvRect.size.width  = letterDef.width;
                uvRect.origin.x    = letterDef.U;
                uvRect.origin.y    = letterDef.V;

                if (letterDef.width <= 0.f || letterDef.height <= 0.f)
                {
                    letter = LabelLetter::create();
                }
                else
                {
                    this->updateBMFontScale();
                    letter =
                        LabelLetter::createWithTexture(_fontAtlas->getTexture(textureID), uvRect, letterDef.rotated);
                    letter->setTextureAtlas(_batchNodes.at(textureID)->getTextureAtlas());
                    letter->setAtlasIndex(letterInfo.atlasIndex);
                    auto px = letterInfo.positionX + _bmfontScale * uvRect.size.width / 2 +
                              _linesOffsetX[letterInfo.lineIndex];
                    auto py = letterInfo.positionY - _bmfontScale * uvRect.size.height / 2 + _letterOffsetY;
                    letter->setPosition(px, py);
                    letter->setOpacity(_realOpacity);
                    this->updateLetterSpriteScale(letter);
                }

                addChild(letter);
                _letters[letterIndex] = letter;
            }
        }
    } while (false);

    return letter;
}

void Label::setLineHeight(float height)
{
    AXASSERT(_currentLabelType != LabelType::STRING_TEXTURE, "Not supported system font!");

    if (_lineHeight != height)
    {
        _lineHeight   = height;
        _contentDirty = true;
    }
}

float Label::getLineHeight() const
{
    AXASSERT(_currentLabelType != LabelType::STRING_TEXTURE, "Not supported system font!");
    return _textSprite ? 0.0f : _lineHeight * _bmfontScale;
}

void Label::setLineSpacing(float height)
{
    if (_lineSpacing != height)
    {
        _lineSpacing  = height;
        _contentDirty = true;
    }
}

float Label::getLineSpacing() const
{
    return _lineSpacing;
}

void Label::setAdditionalKerning(float space)
{

    if (_currentLabelType != LabelType::STRING_TEXTURE)
    {
        if (_additionalKerning != space)
        {
            _additionalKerning = space;
            _contentDirty      = true;
        }
    }
    else
        AXLOG("Label::setAdditionalKerning not supported on LabelType::STRING_TEXTURE");
}

float Label::getAdditionalKerning() const
{
    AXASSERT(_currentLabelType != LabelType::STRING_TEXTURE, "Not supported system font!");

    return _additionalKerning;
}

void Label::computeStringNumLines()
{
    int quantityOfLines = 1;

    if (_utf32Text.empty())
    {
        _numberOfLines = 0;
        return;
    }

    // count number of lines
    size_t stringLen = _utf32Text.length();
    for (size_t i = 0; i < stringLen - 1; ++i)
    {
        if (_utf32Text[i] == StringUtils::UnicodeCharacters::NewLine)
        {
            quantityOfLines++;
        }
    }

    _numberOfLines = quantityOfLines;
}

int Label::getStringNumLines()
{
    if (_contentDirty)
    {
        updateContent();
    }

    if (_currentLabelType == LabelType::STRING_TEXTURE)
    {
        computeStringNumLines();
    }

    return _numberOfLines;
}

int Label::getStringLength()
{
    _lengthOfString = static_cast<int>(_utf32Text.length());
    return _lengthOfString;
}

// RGBA protocol
void Label::setOpacityModifyRGB(bool isOpacityModifyRGB)
{
    if (isOpacityModifyRGB != _isOpacityModifyRGB)
    {
        _isOpacityModifyRGB = isOpacityModifyRGB;
        updateColor();
    }
}

void Label::updateDisplayedColor(const Color3B& parentColor)
{
    Node::updateDisplayedColor(parentColor);

    if (_textSprite)
    {
        _textSprite->updateDisplayedColor(_displayedColor);
    }

    if (_shadowNode)
    {
        _shadowNode->updateDisplayedColor(_displayedColor);
    }

    if (_underlineNode)
    {
        // FIXME: _underlineNode is not a sprite/label. It is a DrawNode
        // and updating its color doesn't work. it must be re-drawn,
        // which makes it super expensive to change update it frequently
        // Correct solution is to update the DrawNode directly since we know it is
        // a line. Returning a pointer to the line is an option
        _contentDirty = true;
    }

    for (auto&& it : _letters)
    {
        it.second->updateDisplayedColor(_displayedColor);
    }
}

void Label::updateDisplayedOpacity(uint8_t parentOpacity)
{
    Node::updateDisplayedOpacity(parentOpacity);

    if (_textSprite)
    {
        _textSprite->updateDisplayedOpacity(_displayedOpacity);
        if (_shadowNode)
        {
            _shadowNode->updateDisplayedOpacity(_displayedOpacity);
        }
    }

    for (auto&& it : _letters)
    {
        it.second->updateDisplayedOpacity(_displayedOpacity);
    }
}

// FIXME: it is not clear what is the difference between setTextColor() and setColor()
// if setTextColor() only changes the text and nothing but the text (no glow, no outline, not underline)
// that's fine but it should be documented
void Label::setTextColor(const Color4B& color)
{
    AXASSERT(_currentLabelType == LabelType::TTF || _currentLabelType == LabelType::STRING_TEXTURE,
             "Only supported system font and ttf!");

    if (_currentLabelType == LabelType::STRING_TEXTURE && _textColor != color)
    {
        _contentDirty = true;
    }

    _textColor    = color;
    _textColorF.r = _textColor.r / 255.0f;
    _textColorF.g = _textColor.g / 255.0f;
    _textColorF.b = _textColor.b / 255.0f;
    _textColorF.a = _textColor.a / 255.0f;
}

void Label::updateColor()
{
    if (_batchNodes.empty())
    {
        return;
    }

    Color4B color4(_displayedColor.r, _displayedColor.g, _displayedColor.b, _displayedOpacity);

    // special opacity for premultiplied textures
    if (_isOpacityModifyRGB)
    {
        color4.r *= _displayedOpacity / 255.0f;
        color4.g *= _displayedOpacity / 255.0f;
        color4.b *= _displayedOpacity / 255.0f;
    }

    ax::TextureAtlas* textureAtlas;
    V3F_C4B_T2F_Quad* quads;
    for (auto&& batchNode : _batchNodes)
    {
        textureAtlas = batchNode->getTextureAtlas();
        quads        = textureAtlas->getQuads();
        auto count   = textureAtlas->getTotalQuads();

        for (int index = 0; index < count; ++index)
        {
            quads[index].bl.colors = color4;
            quads[index].br.colors = color4;
            quads[index].tl.colors = color4;
            quads[index].tr.colors = color4;
            textureAtlas->updateQuad(quads[index], index);
        }
    }
}

std::string Label::getDescription() const
{
    char tmp[50];
    snprintf(tmp, sizeof(tmp), "<Label | Tag = %d, Label = >", _tag);
    std::string ret = tmp;
    ret += _utf8Text;

    return ret;
}

const Vec2& Label::getContentSize() const
{
    if (_systemFontDirty || _contentDirty)
    {
        const_cast<Label*>(this)->updateContent();
    }
    return _contentSize;
}

Rect Label::getBoundingBox() const
{
    const_cast<Label*>(this)->getContentSize();

    return Node::getBoundingBox();
}

void Label::setBlendFunc(const BlendFunc& blendFunc)
{
    _blendFunc      = blendFunc;
    _blendFuncDirty = true;
    if (_textSprite)
    {
        _textSprite->setBlendFunc(blendFunc);
        if (_shadowNode)
        {
            _shadowNode->setBlendFunc(blendFunc);
        }
    }
}

void Label::removeAllChildrenWithCleanup(bool cleanup)
{
    Node::removeAllChildrenWithCleanup(cleanup);
    _letters.clear();
}

void Label::removeChild(Node* child, bool cleanup /* = true */)
{
    Node::removeChild(child, cleanup);
    for (auto&& it : _letters)
    {
        if (it.second == child)
        {
            _letters.erase(it.first);
            break;
        }
    }
}

FontDefinition Label::_getFontDefinition() const
{
    FontDefinition systemFontDef;

    std::string fontName = _systemFont;
    if (_fontAtlas && !_fontAtlas->getFontName().empty())
        fontName = _fontAtlas->getFontName();

    systemFontDef._fontName              = fontName;
    systemFontDef._fontSize              = _systemFontSize;
    systemFontDef._alignment             = _hAlignment;
    systemFontDef._vertAlignment         = _vAlignment;
    systemFontDef._dimensions.width      = _labelWidth == 0.f ? _maxLineWidth : _labelWidth;
    systemFontDef._dimensions.height     = _labelHeight;
    systemFontDef._fontFillColor.r       = _textColor.r;
    systemFontDef._fontFillColor.g       = _textColor.g;
    systemFontDef._fontFillColor.b       = _textColor.b;
    systemFontDef._fontAlpha             = _textColor.a;
    systemFontDef._shadow._shadowEnabled = false;
    systemFontDef._enableWrap            = _enableWrap;
    systemFontDef._overflow              = (int)_overflow;

    if (_currLabelEffect == LabelEffect::OUTLINE && _outlineSize > 0.f)
    {
        systemFontDef._stroke._strokeEnabled = true;
        systemFontDef._stroke._strokeSize    = _outlineSize;
        systemFontDef._stroke._strokeColor.r = _effectColorF.r * 255;
        systemFontDef._stroke._strokeColor.g = _effectColorF.g * 255;
        systemFontDef._stroke._strokeColor.b = _effectColorF.b * 255;
        systemFontDef._stroke._strokeAlpha   = _effectColorF.a * 255;
    }
    else
    {
        systemFontDef._stroke._strokeEnabled = false;
    }

#if (AX_TARGET_PLATFORM != AX_PLATFORM_ANDROID) && (AX_TARGET_PLATFORM != AX_PLATFORM_IOS)
    if (systemFontDef._stroke._strokeEnabled)
    {
        AXLOGERROR("Stroke Currently only supported on iOS and Android!");
    }
    systemFontDef._stroke._strokeEnabled = false;
#endif

    return systemFontDef;
}

void Label::setGlobalZOrder(float globalZOrder)
{
    Node::setGlobalZOrder(globalZOrder);
    if (_textSprite)
    {
        _textSprite->setGlobalZOrder(globalZOrder);
        if (_shadowNode)
        {
            _shadowNode->setGlobalZOrder(globalZOrder);
        }
    }

    if (_underlineNode)
    {
        _underlineNode->setGlobalZOrder(globalZOrder);
    }

#if AX_LABEL_DEBUG_DRAW
    _debugDrawNode->setGlobalZOrder(globalZOrder);
#endif
}

float Label::getRenderingFontSize() const
{
    float fontSize;
    if (_currentLabelType == LabelType::BMFONT)
    {
        fontSize = _bmFontSize;
    }
    else if (_currentLabelType == LabelType::TTF)
    {
        fontSize = this->getTTFConfig().fontSize;
    }
    else if (_currentLabelType == LabelType::STRING_TEXTURE)
    {
        fontSize = _systemFontSize;
    }
    else
    {  // FIXME: find a way to calculate char map font size
        fontSize = this->getLineHeight();
    }
    return fontSize;
}

void Label::enableWrap(bool enable)
{
    if (enable == _enableWrap || _overflow == Overflow::RESIZE_HEIGHT)
    {
        return;
    }

    this->_enableWrap = enable;

    this->rescaleWithOriginalFontSize();

    _contentDirty = true;
}

bool Label::isWrapEnabled() const
{
    return this->_enableWrap;
}

void Label::setOverflow(Overflow overflow)
{
    if (_overflow == overflow)
    {
        return;
    }

    if (_currentLabelType == LabelType::CHARMAP)
    {
        if (overflow == Overflow::SHRINK)
        {
            return;
        }
    }

    if (overflow == Overflow::RESIZE_HEIGHT)
    {
        this->setDimensions(_labelDimensions.width, 0);
        this->enableWrap(true);
    }
    _overflow = overflow;

    this->rescaleWithOriginalFontSize();

    _contentDirty = true;
}

void Label::rescaleWithOriginalFontSize()
{
    auto renderingFontSize = this->getRenderingFontSize();
    if (_originalFontSize - renderingFontSize >= 1)
    {
        this->scaleFontSize(_originalFontSize);
    }
}

Label::Overflow Label::getOverflow() const
{
    return _overflow;
}

void Label::updateLetterSpriteScale(Sprite* sprite)
{
    if (_currentLabelType == LabelType::BMFONT && _bmFontSize > 0)
    {
        sprite->setScale(_bmfontScale);
    }
    else
    {
        if (std::abs(_bmFontSize) < FLT_EPSILON)
        {
            sprite->setScale(0);
        }
        else
        {
            sprite->setScale(1.0);
        }
    }
}

NS_AX_END
