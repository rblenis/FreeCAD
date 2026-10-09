// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2012-2014 Luke Parry <l.parry@warwick.ac.uk>            *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

# include <cmath>

# include <QDomDocument>
# include <QFont>
# include <QFontMetricsF>
# include <QPainterPath>
# include <QFile>
# include <QFontMetrics>
# include <QGraphicsColorizeEffect>
# include <QGraphicsEffect>
# include <QGraphicsSvgItem>
# include <QMap>
# include <QPainter>
# include <QPen>
# include <QSvgRenderer>
# include <QRegularExpression>
# include <QRegularExpressionMatch>

#include <App/Application.h>
#include <Base/Console.h>
#include <Base/Parameter.h>

#include <Mod/TechDraw/App/DrawSVGTemplate.h>
#include <Mod/TechDraw/App/DrawPage.h>
// #include <Mod/TechDraw/App/DrawSVGTemplate.h>
#include <Mod/TechDraw/App/DrawUtil.h>
#include <Mod/TechDraw/App/XMLQuery.h>

#include "QGISVGTemplate.h"
#include "QGIUserTypes.h"
#include "PreferencesGui.h"
#include "QGSPage.h"
#include "Rez.h"
#include "TemplateTextField.h"
#include "ZVALUE.h"
#include "DrawGuiUtil.h"


namespace {

    QMap<QString, QString> parseStyle(QString style) {
        QMap<QString, QString> result;

        QStringList pairs = style.split(';', Qt::SkipEmptyParts);
        for (const QString& pair : pairs) {
            QStringList propVal = pair.split(':');
            if (propVal.length() > 1) {
                result[propVal[0].trimmed()] = propVal[1].trimmed();
            }
        }

        return result;
    }

    double getPointSize(const QString& sz) {
        double ratio = 72.0/96.0; // 72pt per inch, 96 dpi
        int unitLen = 0;

        if (sz.endsWith(QStringLiteral("pt"))) {
            unitLen = 2;
        }
        else if (sz.endsWith(QStringLiteral("mm"))) {
            ratio = 72.0/25.4;
            unitLen = 2;
        }
        else if (sz.endsWith(QStringLiteral("cm"))) {
            ratio = 72.0/2.54;
            unitLen = 2;
        }
        else if (sz.endsWith(QStringLiteral("in"))) {
            ratio = 72.0;
            unitLen = 2;
        }
        else if (sz.endsWith(QStringLiteral("px"))) {
            unitLen = 2;
        }

        return ratio*sz.chopped(unitLen).trimmed().toDouble();
    }

    QFont getFont(QDomElement& elem) {
        if (elem.isNull()) {
            return QFont(QStringLiteral("sans"));
        }

        QDomElement parent = elem.parentNode().toElement();
        QFont result = getFont(parent);

        if (elem.hasAttribute(QStringLiteral("style"))) {
            QMap<QString, QString> style = parseStyle(elem.attribute(QStringLiteral("style")));

            if (style.contains(QStringLiteral("font-family"))) {
                result.setFamily(style[QStringLiteral("font-family")]);
            }
            if (style.contains(QStringLiteral("font-size"))) {
                result.setPointSizeF(getPointSize(style[QStringLiteral("font-size")]));
            }
        }

        if (elem.hasAttribute(QStringLiteral("font-family"))) {
            result.setFamily(elem.attribute(QStringLiteral("font-family")));
        }
        if (elem.hasAttribute(QStringLiteral("font-size"))) {
            result.setPointSizeF(getPointSize(elem.attribute(QStringLiteral("font-size"))));
        }

        return result;
    }

    std::vector<QDomElement> getFCElements(QDomDocument& doc) {
        QDomNodeList textElements = doc.elementsByTagName(QStringLiteral("text"));
        std::vector<QDomElement> filteredTextElements;
        filteredTextElements.reserve(textElements.size());
        for(int i = 0; i < textElements.size(); i++) {
            QDomElement textElement = textElements.at(i).toElement();
            if(textElement.hasAttribute(QStringLiteral(FREECAD_ATTR_EDITABLE))) {
                filteredTextElements.push_back(textElement);
            }
        }
        return filteredTextElements;
    }

    void applyWorkaround(QByteArray& svgCode)
    {
        QDomDocument doc;
        doc.setContent(svgCode);

        // Example <text font-size="12px"><tspan font-size"12px">sadasd</tspan></text>
        // QSvgRenderer::boundsOnElement calculates the bounds of the text element using text width of the `tspan`
        // but faultly the text height of the `text` element that might have another font size. The width
        // of a `tspan will also always be one character too wide.
        // Workaround: apply the font-size of the `tspan` to its parent `text` and remove `tspan` completely
        std::vector<QDomElement> textElements = getFCElements(doc);
        for(QDomElement& textElement : textElements) {
            QDomElement tspan = textElement.firstChildElement(QStringLiteral("tspan"));
            if(tspan.isNull()) {
                continue;
            }

            if(tspan.hasAttribute(QStringLiteral("font-size"))) {
                QString fontSize = tspan.attribute(QStringLiteral("font-size"));
                textElement.setAttribute(QStringLiteral("font-size"), fontSize);
            }
            if(tspan.hasAttribute(QStringLiteral("x"))) {
                QString x = tspan.attribute(QStringLiteral("x"));
                textElement.setAttribute(QStringLiteral("x"), x);
            }
            if(tspan.hasAttribute(QStringLiteral("y"))) {
                QString y = tspan.attribute(QStringLiteral("y"));
                textElement.setAttribute(QStringLiteral("y"), y);
            }

            // Delete tspan, but keep tspan content
            textElement.replaceChild(tspan.firstChild(), tspan);
        }

        // All `text` elements must have an id for using QSvgRenderer::transformForElement later on
        int counter = 0;
        for(QDomElement& textElement : textElements) {
            if (!textElement.hasAttribute(QStringLiteral("id")) ||
                textElement.attribute(QStringLiteral("id")).isEmpty()) {
                QString id = QStringLiteral("freecad_id_") + QString::number(counter);
                textElement.setAttribute(QStringLiteral("id"), id);
                counter++;
            }
        }

        // QSvgRenderer::transformForElement only returns transform for parents, not for the element itself
        // If the `text` element itself has transform, let's wrap it in a shallow group so it's taken into
        // account by QSvgRenderer::transformForElement
        for(QDomElement& textElement : textElements) {
            if (textElement.hasAttribute(QStringLiteral("transform"))) {
                QDomElement group = doc.createElement(QStringLiteral("g"));
                QString transform = textElement.attribute(QStringLiteral("transform"));
                textElement.removeAttribute(QStringLiteral("transform"));
                group.setAttribute(QStringLiteral("transform"), transform);
                textElement.parentNode().replaceChild(group, textElement);
                group.appendChild(textElement);
            }
        }

        svgCode = doc.toByteArray();
    }

// Template text is a few SVG units tall and the item is scaled ~10x to scene
// units, so Qt rasterizes glyphs at 2-3 ppem with a ~10x transform. FreeType
// 2.14 returns Raster_Overflow (0x62) for such glyphs (Noto Sans 'W' first),
// and Qt retries and warns on every repaint. FreeType 2.13 does not. The
// template is painted from a copy with its text converted to filled paths.

// SVG presentation property: attribute first, then the style attribute.
QString svgProp(const QDomElement& e, const QString& name)
{
    if (e.hasAttribute(name)) {
        return e.attribute(name);
    }
    return parseStyle(e.attribute(QStringLiteral("style"))).value(name);
}

// Look on the tspan, then on its text element.
QString svgRunProp(const QDomElement& run, const QDomElement& text, const QString& name)
{
    QString v = svgProp(run, name);
    if (v.isEmpty() && run != text) {
        v = svgProp(text, name);
    }
    return v;
}

double parseSvgFontPx(QString raw)
{
    raw = raw.trimmed();
    if (raw.endsWith(QLatin1String("px"))) {
        raw.chop(2);
        return raw.toDouble();
    }
    if (raw.endsWith(QLatin1String("pt"))) {
        raw.chop(2);
        return raw.toDouble() * 96.0 / 72.0;
    }
    return raw.toDouble();
}

QFont svgOutlineFont(QString family, bool bold)
{
    family = family.trimmed();
    family.remove(QLatin1Char('\''));
    family.remove(QLatin1Char('"'));
    QFont font;
    font.setStyleStrategy(QFont::NoSubpixelAntialias);
    font.setHintingPreference(QFont::PreferNoHinting);
    if (family.isEmpty() || family == QLatin1String("sans-serif") || family == QLatin1String("sans")) {
        font.setFamilies({QStringLiteral("Noto Sans"),
                          QStringLiteral("DejaVu Sans"),
                          QStringLiteral("Liberation Sans")});
        font.setStyleHint(QFont::SansSerif);
    }
    else if (family == QLatin1String("serif")) {
        font.setStyleHint(QFont::Serif);
    }
    else if (family == QLatin1String("monospace")) {
        font.setStyleHint(QFont::TypeWriter);
    }
    else {
        font.setFamily(family);
    }
    if (bold) {
        font.setBold(true);
    }
    return font;
}

QString painterPathToSvg(const QPainterPath& path)
{
    QString d;
    d.reserve(path.elementCount() * 20);
    for (int i = 0; i < path.elementCount(); ++i) {
        const QPainterPath::Element e = path.elementAt(i);
        if (e.isMoveTo()) {
            d += QStringLiteral("M%1 %2").arg(e.x, 0, 'f', 3).arg(e.y, 0, 'f', 3);
        }
        else if (e.isLineTo()) {
            d += QStringLiteral("L%1 %2").arg(e.x, 0, 'f', 3).arg(e.y, 0, 'f', 3);
        }
        else if (e.isCurveTo()) {
            const QPainterPath::Element c2 = path.elementAt(++i);
            const QPainterPath::Element end = path.elementAt(++i);
            d += QStringLiteral("C%1 %2 %3 %4 %5 %6")
                     .arg(e.x, 0, 'f', 3)
                     .arg(e.y, 0, 'f', 3)
                     .arg(c2.x, 0, 'f', 3)
                     .arg(c2.y, 0, 'f', 3)
                     .arg(end.x, 0, 'f', 3)
                     .arg(end.y, 0, 'f', 3);
        }
    }
    return d;
}

QPainterPath svgTextOutline(const QString& content, const QFont& baseFont, double px, double x, double y)
{
    constexpr int kRefPx = 64;
    QFont font(baseFont);
    font.setPixelSize(kRefPx);
    QPainterPath path;
    path.addText(QPointF(0, 0), font, content);
    QTransform xform;
    xform.translate(x, y);
    xform.scale(px / kRefPx, px / kRefPx);
    return xform.map(path);
}

// Replace <text> with filled outlines. Returns the largest font size in SVG user units.
double replaceSvgTextWithOutlines(QDomDocument& doc)
{
    double maxPx = 0.0;
    const QDomNodeList list = doc.elementsByTagName(QStringLiteral("text"));
    QList<QDomElement> texts;
    texts.reserve(list.count());
    for (int i = 0; i < list.count(); ++i) {
        texts.append(list.at(i).toElement());
    }

    for (const QDomElement& textEl : texts) {

        QDomElement group = doc.createElement(QStringLiteral("g"));
        if (textEl.hasAttribute(QStringLiteral("transform"))) {
            group.setAttribute(QStringLiteral("transform"), textEl.attribute(QStringLiteral("transform")));
        }
        if (textEl.hasAttribute(QStringLiteral("id"))) {
            group.setAttribute(QStringLiteral("id"), textEl.attribute(QStringLiteral("id")));
        }

        auto appendRun = [&](const QString& content, const QDomElement& src, double fallbackX, double fallbackY) {
            const QString trimmed = content.trimmed();
            if (trimmed.isEmpty()) {
                return;
            }
            const QString family = svgRunProp(src, textEl, QStringLiteral("font-family"));
            const QString weight = svgRunProp(src, textEl, QStringLiteral("font-weight"));
            const bool bold = weight == QLatin1String("bold") || weight.toInt() >= 600;
            double px = parseSvgFontPx(svgRunProp(src, textEl, QStringLiteral("font-size")));
            if (px <= 0.0) {
                px = 3.5;
            }
            // QtSvg truncates the size to whole pixels (QFont::setPixelSize(int)).
            // Match it so the outlined text keeps today's size on screen and in exports.
            px = std::max(1.0, std::floor(px));
            maxPx = std::max(maxPx, px);

            const QString anchor = svgRunProp(src, textEl, QStringLiteral("text-anchor"));
            double x = src.attribute(QStringLiteral("x"), textEl.attribute(QStringLiteral("x"))).toDouble();
            double y = src.attribute(QStringLiteral("y"), textEl.attribute(QStringLiteral("y"))).toDouble();
            if (!src.hasAttribute(QStringLiteral("x")) && !textEl.hasAttribute(QStringLiteral("x"))) {
                x = fallbackX;
            }
            if (!src.hasAttribute(QStringLiteral("y")) && !textEl.hasAttribute(QStringLiteral("y"))) {
                y = fallbackY;
            }

            const QFont font = svgOutlineFont(family, bold);
            if (anchor == QLatin1String("middle") || anchor == QLatin1String("end")) {
                QFont sized(font);
                sized.setPixelSize(64);
                const double width = QFontMetricsF(sized).horizontalAdvance(content) * (px / 64.0);
                x -= (anchor == QLatin1String("middle")) ? width / 2.0 : width;
            }

            const QPainterPath path = svgTextOutline(content, font, px, x, y);
            if (path.isEmpty()) {
                return;
            }
            QDomElement pel = doc.createElement(QStringLiteral("path"));
            pel.setAttribute(QStringLiteral("d"), painterPathToSvg(path));
            QString fill = svgRunProp(src, textEl, QStringLiteral("fill"));
            if (fill.isEmpty()) {
                fill = QStringLiteral("#000000");
            }
            pel.setAttribute(QStringLiteral("fill"), fill);
            pel.setAttribute(QStringLiteral("stroke"), QStringLiteral("none"));
            group.appendChild(pel);
        };

        bool any = false;
        for (QDomNode child = textEl.firstChild(); !child.isNull(); child = child.nextSibling()) {
            if (child.isElement() && child.toElement().tagName() == QLatin1String("tspan")) {
                appendRun(child.toElement().text(), child.toElement(), 0.0, 0.0);
                any = true;
            }
            else if (child.isText()) {
                const QString data = child.toText().data();
                if (!data.trimmed().isEmpty()) {
                    appendRun(data, textEl, 0.0, 0.0);
                    any = true;
                }
            }
        }
        if (any && group.hasChildNodes()) {
            textEl.parentNode().replaceChild(group, textEl);
        }
    }
    return maxPx;
}

class OutlineSvgItem : public QGraphicsSvgItem
{
public:
    explicit OutlineSvgItem(QGraphicsItem* parent = nullptr)
        : QGraphicsSvgItem(parent)
    {}
    ~OutlineSvgItem() override
    {
        delete m_outline;
    }

    void setOutline(QByteArray svg, double maxFontPx)
    {
        delete m_outline;
        m_outline = new QSvgRenderer();
        m_outline->load(svg);
        m_maxFontPx = maxFontPx;
    }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override
    {
        if (m_outline && m_outline->isValid() && m_maxFontPx > 0.0) {
            m_outline->render(painter, boundingRect());
            return;
        }
        QGraphicsSvgItem::paint(painter, option, widget);
    }

private:
    QSvgRenderer* m_outline = nullptr;
    double m_maxFontPx = 0.0;
};

}  // anonymous namespace


using namespace TechDrawGui;
using namespace TechDraw;

QGISVGTemplate::QGISVGTemplate(QGSPage* scene) : QGITemplate(scene),
    m_svgItem(new OutlineSvgItem(this)),
    m_svgRender(new QSvgRenderer()),
    m_pageRectangle(new QGraphicsRectItem(this))
{
    m_pageRectangle->setZValue(ZVALUE::BACKGROUND);

    m_svgItem->setSharedRenderer(m_svgRender);

    m_svgItem->setFlags(QGraphicsItem::ItemClipsToShape);
    m_svgItem->setCacheMode(QGraphicsItem::NoCache);

    addToGroup(m_svgItem);

    m_svgItem->setZValue(ZVALUE::SVGTEMPLATE);
    setZValue(ZVALUE::SVGTEMPLATE);
}

QGISVGTemplate::~QGISVGTemplate() { delete m_svgRender; }

void QGISVGTemplate::openFile(const QFile& file) { Q_UNUSED(file); }

void QGISVGTemplate::load(QByteArray svgCode)
{
    prepareGeometryChange();
    applyWorkaround(svgCode);
    m_svgRender->load(svgCode);

    // Second copy with <text> turned into filled paths. See svgProp above.
    {
        QDomDocument outlineDoc;
        if (outlineDoc.setContent(svgCode)) {
            const double maxPx = replaceSvgTextWithOutlines(outlineDoc);
            static_cast<OutlineSvgItem*>(m_svgItem)->setOutline(outlineDoc.toByteArray(), maxPx);
        }
    }

    QSize size = m_svgRender->defaultSize();
    m_svgItem->setSharedRenderer(m_svgRender);

    //convert from pixels or mm or inches in svg file to mm page size
    TechDraw::DrawSVGTemplate* tmplte = getSVGTemplate();
    double xaspect = tmplte->getWidth() / static_cast<double>(size.width());
    double yaspect = tmplte->getHeight() / static_cast<double>(size.height());

    QTransform qtrans;
    qtrans.translate(0.0, Rez::guiX(-tmplte->getHeight()));
    qtrans.scale(Rez::guiX(xaspect), Rez::guiX(yaspect));
    m_svgItem->setTransform(qtrans);

    if (Preferences::lightOnDark()) {
        auto color = PreferencesGui::getAccessibleQColor(QColor(Qt::black));
        auto* colorizeEffect = new QGraphicsColorizeEffect();
        colorizeEffect->setColor(color);
        m_svgItem->setGraphicsEffect(colorizeEffect);
    }
    else {
        //remove and delete any existing graphics effect
        if (m_svgItem->graphicsEffect()) {
            m_svgItem->setGraphicsEffect(nullptr);
        }
    }
}

TechDraw::DrawSVGTemplate* QGISVGTemplate::getSVGTemplate() const
{
    if (pageTemplate && pageTemplate->isDerivedFrom<TechDraw::DrawSVGTemplate>()) {
        return static_cast<TechDraw::DrawSVGTemplate*>(pageTemplate);
    }

    return nullptr;
}

void QGISVGTemplate::draw()
{
    TechDraw::DrawSVGTemplate* tmplte = getSVGTemplate();
    if (!tmplte) {
        throw Base::RuntimeError("Template Feature not set for QGISVGTemplate");
    }

    drawPageRectangle();

    QString templateSvg = tmplte->processTemplate();
    load(templateSvg.toUtf8());

    clearClickHandles();
    createClickHandles();
}

void QGISVGTemplate::drawPageRectangle()
{
    // Draw the white page
    // Default to A3 landscape, though this is currently relevant
    // only for opening corrupt docs, etc.
    constexpr double PageWidthDefault{420.0};
    double pageWidth{PageWidthDefault};
    constexpr double PageHeightDefault{297.0};
    double pageHeight{PageHeightDefault};

    DrawTemplate* ourTemplate = getTemplate();
    DrawPage* ourPage = ourTemplate ? ourTemplate->getParentPage() : nullptr;
    if (ourTemplate && ourPage) {
        pageWidth = Rez::guiX(ourPage->getPageWidth());
        pageHeight = Rez::guiX(ourPage->getPageHeight());
    }
    QRectF paperRect(0, -pageHeight, pageWidth, pageHeight);
    QBrush pageBrush(PreferencesGui::pageQColor());
    m_pageRectangle->setRect(paperRect);
    m_pageRectangle->setBrush(pageBrush);
}


void QGISVGTemplate::updateView(bool update)
{
    Q_UNUSED(update);
    draw();
}

std::vector<TemplateTextField*> QGISVGTemplate::getTextFields()
{
    std::vector<TemplateTextField*> result;
    result.reserve(childItems().size());

    QList<QGraphicsItem*> templateChildren = childItems();
    for (auto& child : templateChildren) {
        if (child->type() == UserType::TemplateTextField) {
            result.push_back(static_cast<TemplateTextField*>(child));
        }
    }

    return result;
}

void QGISVGTemplate::clearClickHandles()
{
    prepareGeometryChange();
    std::vector<TemplateTextField*> textFields = getTextFields();
    for (auto& textField : textFields) {
        textField->hide();
        scene()->removeItem(textField);
        delete textField;
     }
}

void QGISVGTemplate::createClickHandles()
{
    auto* qgsp = static_cast<QGSPage*>(scene());
    if (qgsp->getExportingAny()) {
        // no click handles on export
        return;
    }

    prepareGeometryChange();
    TechDraw::DrawSVGTemplate* svgTemplate = getSVGTemplate();
    if (svgTemplate->isRestoring()) {
        //the embedded file is not available yet, so just return
        return;
    }

    QByteArray svgCode = svgTemplate->processTemplate().toUtf8();
    applyWorkaround(svgCode);
    QDomDocument templateDocument;
    if (!templateDocument.setContent(svgCode)) {
        Base::Console().message("QGISVGTemplate::createClickHandles - xml loading error\n");
        return;
    }

    //TODO: Find location of special fields (first/third angle) and make graphics items for them

    std::vector<QDomElement> textElements = getFCElements(templateDocument);
    for(QDomElement& textElement : textElements) {
        // Get elements bounding box of text
        QString id = textElement.attribute(QStringLiteral("id"));
        QRectF textRect = m_svgRender->boundsOnElement(id);
        QString name = textElement.attribute(QStringLiteral(FREECAD_ATTR_EDITABLE));
        QDomElement tspan = textElement.firstChildElement();
        QFont font = getFont(tspan.isNull() ? textElement : tspan);
        QFontMetricsF fm(font);

        // deal with empty text
        QString content = QString::fromStdString(svgTemplate->EditableTexts.getValue(name.toStdString()));
        bool isShortText = content.isEmpty();

#if QT_VERSION < QT_VERSION_CHECK(6, 2, 0)
        // from PR 27117
        if (!textRect.isValid()) {
            // This is a Qt5 workaround for boundsOnElement() issue fixed in Qt6.2.0
            // Once Qt6 is mandatory, this code may be safely removed.
            // For more details please see https://qt-project.atlassian.net/browse/QTBUG-32405
            if (isShortText) {
                textRect = fm.boundingRect(QStringLiteral("_"));
            }
            else {
                textRect = fm.boundingRect(content);
            }
            if (textRect.isValid()) {
                textRect = m_svgRender->transformForElement(id).mapRect(textRect);
                textRect.translate(textElement.attribute(QStringLiteral("x")).toDouble(),
                                   textElement.attribute(QStringLiteral("y")).toDouble());

                QMap<QString, QString> styleMap = parseStyle(textElement.attribute(QStringLiteral("style")));
                QString textAnchor = styleMap[QStringLiteral("text-anchor")];
                if (textAnchor.compare(QStringLiteral("middle")) == 0) {
                    textRect.translate(-textRect.width()/2.0, 0.0);
                }
                else if (textAnchor.compare(QStringLiteral("end")) == 0) {
                    textRect.translate(-textRect.width(), 0.0);
                }
            }
        }
#endif

        if (isShortText) {
            // if there is no content, the bounding rect will be oversized and the rect may obscure other
            // fields and make them unselectable. The calculated box is slightly out of position, but
            // will correct itself once the content is no longer empty.
            constexpr double MinRectHeight{3.25};   // roughly 3.5px in the svg
            constexpr double MinRectWidth{2.381};   // roughly width of '_' character @ 3.5px
            constexpr double UpwardRectShift{1.75}; // magic. Eliminates some dead space in br of empty text.
            textRect.setBottom(textRect.bottom() - UpwardRectShift);
            textRect.setTop(textRect.bottom() - MinRectHeight);
            textRect.setRight(textRect.left() + MinRectWidth);
        }

        // Get tight bounding box of text
        double factor = textRect.height() / fm.height();  // Correcting font metrics and SVG text due to different font sizes
        QRectF tightTextRect = textRect.adjusted(0.0, 0.0, 0.0, -fm.descent() * factor);
        tightTextRect.setTop(tightTextRect.bottom() - (fm.capHeight() * factor));

        // Transform tight bounding box of text
        QPolygonF tightTextPoly(tightTextRect);  // Polygon because rect cannot be rotated
        QTransform SVGTransform = m_svgRender->transformForElement(id);
        tightTextPoly = SVGTransform.map(tightTextPoly);  // SVGTransform always before templateTransform
        QTransform templateTransform;
        templateTransform.translate(0.0, Rez::guiX(-getSVGTemplate()->getHeight()));
        templateTransform.scale(Rez::getRezFactor(), Rez::getRezFactor());
        tightTextPoly = templateTransform.map(tightTextPoly);

        auto item(new TemplateTextField(this, svgTemplate, name.toStdString()));
        item->setAutofillId(textElement.attribute(QStringLiteral(FREECAD_ATTR_AUTOFILL)).toStdString());

        constexpr double TopPadFactor{0.15};
        constexpr double BottomPadFactor{0.2};
        QMarginsF padding(
            0.0,
            TopPadFactor * tightTextRect.height(),
            0.0,
            BottomPadFactor * tightTextRect.height()
        );
        QRectF clickrect = tightTextRect.marginsAdded(padding);
        QPolygonF clickpoly = SVGTransform.map(clickrect);
        clickpoly = templateTransform.map(clickpoly);
        item->setRectangle(clickpoly.boundingRect());  // TODO: templateTextField doesn't support polygon yet

        QPointF bottomLeft = clickpoly.at(3);
        QPointF bottomRight = clickpoly.at(2);
        item->setLine(bottomLeft, bottomRight);
        item->setLineColor(PreferencesGui::templateClickBoxColor());
        item->setZValue(ZVALUE::SVGTEMPLATE + 1);

        item->setShortText(isShortText);
        if (isShortText) {
            item->showLine();
        } else {
            item->hideLine();
        }

        addToGroup(item);
    }
}


#include <Mod/TechDraw/Gui/moc_QGISVGTemplate.cpp>
