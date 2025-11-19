#include "GGraph.h"
#include "utils/ByteBuffer.h"
#include "utils/ToolSet.h"

NS_FGUI_BEGIN
using namespace ax;

static void drawVertRect(ax::DrawNode* shape, float x, float y, float width, float height, const ax::Color4B& color)
{
    float mx = x + width;
    float my = y + height;
    shape->drawTriangle(Vec2(x, y), Vec2(mx, y), Vec2(x, my), color);
    shape->drawTriangle(Vec2(mx, y), Vec2(mx, my), Vec2(x, my), color);
}

GGraph::GGraph() : _shape(nullptr),
                   _type(0),
                   _lineSize(1),
                   _lineColor(Color4B::BLACK),
                   _fillColor(Color4B::WHITE),
                   _cornerRadius(nullptr),
                   _polygonPoints(nullptr),
                   _distances(nullptr)
{
    _touchDisabled = true;
}

GGraph::~GGraph()
{
    AX_SAFE_DELETE(_cornerRadius);
    AX_SAFE_DELETE(_polygonPoints);
    AX_SAFE_DELETE(_distances);
}

void GGraph::handleInit()
{
    _shape = DrawNode::create();
    _shape->retain();

    _displayObject = _shape;
}

void GGraph::drawRect(float aWidth, float aHeight, int lineSize, const ax::Color4B& lineColor, const ax::Color4B& fillColor)
{
    _type = 0; //avoid updateshape call in handleSizeChange
    setSize(aWidth, aHeight);
    _type = 1;
    _lineSize = lineSize;
    _lineColor = lineColor;
    _fillColor = fillColor;
    updateShape();
}

void GGraph::drawEllipse(float aWidth, float aHeight, int lineSize, const ax::Color4B& lineColor, const ax::Color4B& fillColor)
{
    _type = 0; //avoid updateshape call in handleSizeChange
    setSize(aWidth, aHeight);
    _type = 2;
    _lineSize = lineSize;
    _lineColor = lineColor;
    _fillColor = fillColor;
    updateShape();
}

void GGraph::drawPolygon(int lineSize, const ax::Color4B& lineColor, const ax::Color4B& fillColor, const ax::Vec2* points, int count)
{
    _type = 3;
    _lineSize = lineSize;
    _lineColor = lineColor;
    _fillColor = fillColor;
    if (_polygonPoints == nullptr)
        _polygonPoints = new std::vector<Vec2>();
    else
        _polygonPoints->clear();
    float h = getHeight();
    _polygonPointOffset = h;
    for (int i = 0; i < count; i++)
    {
        Vec2 pt = *(points + i);
        pt.y = h - pt.y;
        _polygonPoints->push_back(*(points + i));
    }
    updateShape();
}

void GGraph::drawRegularPolygon(int lineSize, const ax::Color4B& lineColor, const ax::Color4B& fillColor,
                                int sides, float startAngle, const float* distances, int count)
{
    _type = 4;
    _lineSize = lineSize;
    _lineColor = lineColor;
    _fillColor = fillColor;
    _sides = sides;
    _startAngle = startAngle;
    if (distances != nullptr)
    {
        if (_distances == nullptr)
            _distances = new std::vector<float>();
        else
            _distances->clear();
        for (int i = 0; i < count; i++)
            _distances->push_back(*(distances + i));
    }
    else if (_distances != nullptr)
        _distances->clear();
}

void GGraph::updateShape()
{
    _shape->clear();
    if (_type == 0)
    {
        _touchDisabled = false;
        return;
    }

    switch (_type)
    {
    case 1:
    {
        if (_customMap.contains("radius")) {
            float x = 0;
            float y = 0;
            float w = _size.width;
            float h = _size.height;
            float radius = std::stof(_customMap["radius"]);
            float r = std::min(radius, std::min(w, h) / 2.0f)-0.1;
            int segments = 30;

            // 四个角的圆心
            ax::Vec2 centers[4] = {
                    ax::Vec2(x + r,     y + r),      // 左下
                    ax::Vec2(x + w - r, y + r),      // 右下
                    ax::Vec2(x + w - r, y + h - r),  // 右上
                    ax::Vec2(x + r,     y + h - r),  // 左上
            };

            // 生成圆角点
            std::vector<ax::Vec2> points;
            auto drawCorner = [&](const ax::Vec2& center, float startAngle) {
                for (int i = 0; i <= segments; ++i) {
                    float angle = startAngle + (M_PI_2) * (float(i) / segments);
                    points.push_back(ax::Vec2(center.x + r * cosf(angle), center.y + r * sinf(angle)));
                }
            };

            // 左下角（180°~270°）
            drawCorner(centers[0], M_PI);
            // 右下角（270°~360°）
            drawCorner(centers[1], M_PI * 1.5f);
            // 右上角（0°~90°）
            drawCorner(centers[2], 0.0f);
            // 左上角（90°~180°）
            drawCorner(centers[3], M_PI_2);

            // 填充
            if (_fillColor.a > 0) {
                _shape->drawSolidPoly(points.data(), points.size(), _fillColor);
            }
            // 边框
            if (_lineSize > 0 && _lineColor.a > 0) {
                _shape->drawPoly(points.data(), points.size(), true, _lineColor, _lineSize);
            }
        }
        else if (_lineSize > 0)
        {
            float wl = _size.width - _lineSize;
            float hl = _size.height - _lineSize;
            drawVertRect(_shape, 0, 0, wl, _lineSize, _lineColor);
            drawVertRect(_shape, wl, 0, _lineSize, hl, _lineColor);
            drawVertRect(_shape, _lineSize, hl, wl, _lineSize, _lineColor);
            drawVertRect(_shape, 0, _lineSize, _lineSize, hl, _lineColor);

            drawVertRect(_shape, _lineSize, _lineSize, _size.width - _lineSize * 2, _size.height - _lineSize * 2, _fillColor);
        }
        else
            drawVertRect(_shape, 0, 0, _size.width, _size.height, _fillColor);
        break;
    }
    case 2:
    {
        if (_lineSize > 0)
        {
            _shape->drawCircle(Vec2(_size.width / 2, _size.height / 2), _size.width / 2, 0, 360, false, 1, _size.height / _size.width, _lineColor, _lineSize);
        }
        _shape->drawSolidCircle(Vec2(_size.width / 2, _size.height / 2), _size.width / 2, 0, 360, 1, _size.height / _size.width, _fillColor);
        break;
    }
    case 3:
    {
        _shape->drawPolygon(_polygonPoints->data(), (int)_polygonPoints->size(), _fillColor, _lineSize * 0.5f, _lineColor);
        break;
    }

    case 4:
    {
        float h = getHeight();
        _polygonPointOffset = h;
        if (_polygonPoints == nullptr)
            _polygonPoints = new std::vector<Vec2>();
        else
            _polygonPoints->clear();

        float radius = MIN(getWidth(), getHeight()) * 0.5f;
        float angle = MATH_DEG_TO_RAD(_startAngle);
        float deltaAngle = 2 * M_PI / _sides;
        float dist;
        for (int i = 0; i < _sides; i++)
        {
            if (_distances != nullptr && i < (int)_distances->size())
                dist = (*_distances)[i];
            else
                dist = 1;

            float xv = radius + radius * dist * cos(angle);
            float yv = h - (radius + radius * dist * sin(angle));
            _polygonPoints->push_back(Vec2(xv, yv));

            angle += deltaAngle;
        }

        _shape->drawPolygon(_polygonPoints->data(), (int)_polygonPoints->size(), _fillColor, _lineSize * 0.5f, _lineColor);

        break;
    }
    }
}

ax::Color3B GGraph::getColor() const
{
    return (Color3B)_fillColor;
}

void GGraph::setColor(const ax::Color3B& value)
{
    _fillColor = Color4B(value, _fillColor.a);
    updateShape();
}

ax::Value GGraph::getProp(ObjectPropID propId)
{
    switch (propId)
    {
    case ObjectPropID::Color:
        return Value(ToolSet::colorToInt(getColor()));
    default:
        return GObject::getProp(propId);
    }
}

void GGraph::setProp(ObjectPropID propId, const ax::Value& value)
{
    switch (propId)
    {
    case ObjectPropID::Color:
        setColor(ToolSet::intToColor(value.asUnsignedInt()));
        break;
    default:
        GObject::setProp(propId, value);
        break;
    }
}

void GGraph::handleSizeChanged()
{
    GObject::handleSizeChanged();

    if (_type == 3 || _type == 4)
    {
        float h = getHeight();
        int count = (int)_polygonPoints->size();
        for (int i = 0; i < count; i++)
        {
            Vec2 pt = (*_polygonPoints)[i];
            pt.y = h - (_polygonPointOffset - pt.y);
            (*_polygonPoints)[i] = pt;
        }
        _polygonPointOffset = h;
    }

    updateShape();
}

void GGraph::setup_beforeAdd(ByteBuffer* buffer, int beginPos)
{
    GObject::setup_beforeAdd(buffer, beginPos);

    buffer->seek(beginPos, 5);

    _type = buffer->readByte();
    if (_type != 0)
    {
        _lineSize = buffer->readInt();
        _lineColor = (Color4B)buffer->readColor();
        _fillColor = (Color4B)buffer->readColor();
        UIConfig::convertToThemeColor(_fillColor);
        if (buffer->readBool())
        {
            _cornerRadius = new float[4];
            for (int i = 0; i < 4; i++)
                _cornerRadius[i] = buffer->readFloat();
        }

        if (_type == 3)
        {
            int cnt = buffer->readShort() / 2;
            _polygonPoints = new std::vector<Vec2>(cnt);
            float h = getHeight();
            _polygonPointOffset = h;
            for (int i = 0; i < cnt; i++)
            {
                float f1 = buffer->readFloat();
                float f2 = h - buffer->readFloat();
                (*_polygonPoints)[i] = Vec2(f1, f2);
            }
        }
        else if (_type == 4)
        {
            _sides = buffer->readShort();
            _startAngle = buffer->readFloat();
            int cnt = buffer->readShort();
            if (cnt > 0)
            {
                _distances = new std::vector<float>(cnt);
                for (int i = 0; i < cnt; i++)
                    (*_distances)[i] = buffer->readFloat();
            }
        }

        updateShape();
    }
}

NS_FGUI_END
