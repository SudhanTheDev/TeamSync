#include "PremiumWidgets.h"
#include "ThemeManager.h"

#include <QApplication>
#include <QEnterEvent>
#include <QDialog>
#include <QEvent>
#include <QFileDialog>
#include <QGraphicsDropShadowEffect>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionButton>
#include <QParallelAnimationGroup>
#include <QPointer>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPropertyAnimation>
#include <QVariantAnimation>
#include <QTimer>
#include <QRegularExpression>

#include <algorithm>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace teamsync::ui {
namespace {
bool dark() { return qApp->property("darkTheme").toBool(); }
QColor mix(const QColor& a, const QColor& b, qreal weight) {
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * weight,
        a.greenF() + (b.greenF() - a.greenF()) * weight,
        a.blueF() + (b.blueF() - a.blueF()) * weight,
        a.alphaF() + (b.alphaF() - a.alphaF()) * weight);
}
QColor toneColor(const QString& tone) {
    if (tone == "green") return QColor(dark() ? "#44d58a" : "#09a557");
    if (tone == "purple") return QColor(dark() ? "#ad8cff" : "#8753f8");
    if (tone == "amber") return QColor(dark() ? "#ffba4d" : "#f3a10a");
    if (tone == "red") return QColor(dark() ? "#ff8791" : "#f04d65");
    return accentColor();
}
void drawIcon(QPainter& painter, const QString& name, const QRect& rect, const QColor& color) {
    const qreal ratio = painter.device()->devicePixelRatioF();
    QPixmap source = ThemeManager::icon(name, dark()).pixmap(rect.size() * ratio);
    if (source.isNull()) return;
    source.setDevicePixelRatio(ratio);
    QPainter tint(&source);
    tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
    tint.fillRect(source.rect(), color);
    tint.end();
    painter.drawPixmap(rect, source);
}
QColor avatarColor(const QString& name) {
    const QList<QColor> colors = {QColor("#6c91c9"), QColor("#ae8464"), QColor("#5b9a86"),
                                  QColor("#8a78b3"), QColor("#bc7881"), QColor("#598faf")};
    return colors[static_cast<int>(qHash(name) % colors.size())];
}
void drawAvatar(QPainter& painter, const QRectF& rect, const QString& name) {
    painter.setPen(QPen(dark() ? QColor("#1b2431") : QColor(Qt::white), 2));
    painter.setBrush(avatarColor(name));
    painter.drawEllipse(rect);
    QFont font = QApplication::font();
    font.setPixelSize(std::max(9, static_cast<int>(rect.height() * .34)));
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(rect, Qt::AlignCenter, initials(name));
}
class FadeOverlay final : public QWidget {
public:
    FadeOverlay(QWidget* parent, QPixmap previous, int duration)
        : QWidget(parent), image_(std::move(previous)) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setObjectName("pageFadeOverlay");
        setGeometry(parent->rect());
        auto* fade = new QVariantAnimation(this);
        fade->setDuration(duration);
        fade->setStartValue(1.0);
        fade->setEndValue(0.0);
        fade->setEasingCurve(QEasingCurve::OutCubic);
        connect(fade, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { opacity_ = v.toReal(); update(); });
        connect(fade, &QVariantAnimation::finished, this, [this] {
            hide();
            if (parentWidget()) parentWidget()->update();
            deleteLater();
        });
        // Qt may throttle animation ticks for an occluded/background window.
        // Never leave the previous page covering live content indefinitely.
        QTimer::singleShot(duration + 100, this, [this] {
            hide();
            if (parentWidget()) parentWidget()->update();
            deleteLater();
        });
        show();
        raise();
        fade->start(QAbstractAnimation::DeleteWhenStopped);
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setOpacity(opacity_);
        painter.drawPixmap(rect(), image_);
    }
private:
    QPixmap image_;
    qreal opacity_ = 1;
};

class DialogMotionFilter final : public QObject {
public:
    explicit DialogMotionFilter(QWidget* root) : QObject(root), root_(root) {}
protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        auto* dialog = qobject_cast<QDialog*>(watched);
        if (!root_ || !dialog || !root_->isAncestorOf(dialog)) return false;
        if (event->type() == QEvent::Show) {
            ThemeManager::applyNativeTitleBar(dialog, dark());
            if (!motionAllowed() || dialog->property("manualEntrance").toBool() ||
                dialog->objectName() == "connectionToast" ||
                qobject_cast<QProgressDialog*>(dialog) || qobject_cast<QFileDialog*>(dialog)) return false;
            dialog->setWindowOpacity(0.0);
            const QPointer<QDialog> guard(dialog);
            QTimer::singleShot(0, dialog, [guard] {
                if (!guard || !guard->isVisible()) return;
                auto* previous = guard->findChild<QParallelAnimationGroup*>("dialogEntranceAnimation", Qt::FindDirectChildrenOnly);
                if (previous) { previous->stop(); previous->deleteLater(); }
                const QPoint destination = guard->pos();
                guard->setProperty("entranceTargetPos", destination);
                auto* entrance = new QParallelAnimationGroup(guard);
                entrance->setObjectName("dialogEntranceAnimation");
                auto* fade = new QPropertyAnimation(guard, "windowOpacity", entrance);
                fade->setDuration(180);
                fade->setStartValue(0.0);
                fade->setEndValue(1.0);
                fade->setEasingCurve(QEasingCurve::OutCubic);
                entrance->addAnimation(fade);
                // Only top-level dialog windows move; Qt layouts retain ownership
                // of every ordinary page and child widget's geometry.
                if (guard->isWindow()) {
                    auto* move = new QPropertyAnimation(guard, "pos", entrance);
                    move->setDuration(200);
                    move->setStartValue(destination + QPoint(0, 8));
                    move->setEndValue(destination);
                    move->setEasingCurve(QEasingCurve::OutCubic);
                    entrance->addAnimation(move);
                }
                entrance->start(QAbstractAnimation::DeleteWhenStopped);
            });
        } else if (event->type() == QEvent::Hide) {
            auto* entrance = dialog->findChild<QParallelAnimationGroup*>("dialogEntranceAnimation", Qt::FindDirectChildrenOnly);
            if (entrance) { entrance->stop(); entrance->deleteLater(); }
            dialog->setWindowOpacity(1.0);
            const QVariant target = dialog->property("entranceTargetPos");
            if (target.isValid()) dialog->move(target.toPoint());
        }
        return false;
    }
private:
    QPointer<QWidget> root_;
};
} // namespace

bool motionAllowed() {
    if (qApp->property("animationsEnabled").isValid() && !qApp->property("animationsEnabled").toBool()) return false;
#ifdef Q_OS_WIN
    BOOL enabled = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) && !enabled) return false;
#endif
    return true;
}
QColor accentColor() { return QColor(dark() ? "#419aff" : "#087bff"); }
QString initials(const QString& name) {
    const auto parts = name.simplified().split(' ', Qt::SkipEmptyParts);
    if (parts.isEmpty()) return "?";
    QString value = parts.first().left(1);
    if (parts.size() > 1) value += parts.last().left(1);
    return value.toUpper();
}
QIcon avatarIcon(const QString& name, int size) {
    QIcon icon;
    for (int scale : {1, 2}) {
        QPixmap image(size * scale, size * scale);
        image.fill(Qt::transparent);
        image.setDevicePixelRatio(scale);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        drawAvatar(painter, QRectF(1, 1, size - 2, size - 2), name);
        painter.end();
        icon.addPixmap(image);
    }
    return icon;
}
void crossFade(QWidget* page, const QPixmap& previous, int duration) {
    if (!page) return;
    for (auto* overlay : page->findChildren<QWidget*>("pageFadeOverlay", Qt::FindDirectChildrenOnly)) {
        overlay->hide();
        overlay->deleteLater();
    }
    if (page->isVisible() && !previous.isNull() && motionAllowed()) new FadeOverlay(page, previous, duration);
}
void softShadow(QWidget* widget, int blur, int yOffset) {
    auto* effect = new QGraphicsDropShadowEffect(widget);
    effect->setBlurRadius(blur);
    effect->setOffset(0, yOffset);
    effect->setColor(QColor(47, 87, 143, dark() ? 20 : 14));
    widget->setGraphicsEffect(effect);
}

void installMotionEffects(QWidget* root) {
    if (!root || root->property("motionEffectsInstalled").toBool()) return;
    root->setProperty("motionEffectsInstalled", true);
    qApp->installEventFilter(new DialogMotionFilter(root));
}

void animateProgress(QProgressBar* progress, const int value) {
    if (!progress) return;
    const int target = std::clamp(value, progress->minimum(), progress->maximum());
    auto* previous = progress->findChild<QPropertyAnimation*>("progressValueAnimation", Qt::FindDirectChildrenOnly);
    if (previous) { previous->stop(); previous->deleteLater(); }
    if (!motionAllowed() || progress->value() == target || progress->minimum() == progress->maximum()) {
        progress->setValue(target);
        return;
    }
    auto* animation = new QPropertyAnimation(progress, "value", progress);
    animation->setObjectName("progressValueAnimation");
    animation->setDuration(240);
    animation->setStartValue(std::max(progress->minimum(), progress->value()));
    animation->setEndValue(target);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

ReferenceItemDelegate::ReferenceItemDelegate(QObject* parent) : QStyledItemDelegate(parent) {}
QSize ReferenceItemDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    auto size = QStyledItemDelegate::sizeHint(option, index);
    size.setHeight(std::max(size.height(), 40));
    return size;
}
void ReferenceItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    const auto kind = static_cast<CellKind>(index.data(KindRole).toInt());
    if (kind == CellKind::Normal && index.data(Qt::CheckStateRole).isValid()) {
        QStyleOptionViewItem base(option);
        initStyleOption(&base, index);
        base.features &= ~QStyleOptionViewItem::HasCheckIndicator;
        base.text.clear();
        auto* style = option.widget ? option.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &base, painter, option.widget);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QRectF box(option.rect.center().x() - 7, option.rect.center().y() - 7, 14, 14);
        const bool checked = index.data(Qt::CheckStateRole).toInt() == Qt::Checked;
        const QColor edge = checked ? accentColor() : QColor(dark() ? "#93A6C2" : "#9AAAC0");
        painter->setPen(QPen(edge, 1));
        painter->setBrush(checked ? accentColor() : QColor(dark() ? "#192029" : "#FEFEFE"));
        painter->drawRoundedRect(box, 3, 3);
        if (checked) {
            painter->setPen(QPen(Qt::white, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter->drawPolyline(QPolygonF({box.topLeft() + QPointF(3, 7), box.topLeft() + QPointF(6, 10),
                box.topLeft() + QPointF(11, 4)}));
        }
        painter->restore();
        return;
    }
    if (kind == CellKind::Normal) { QStyledItemDelegate::paint(painter, option, index); return; }
    QStyleOptionViewItem base(option);
    initStyleOption(&base, index);
    base.text.clear();
    QApplication::style()->drawControl(QStyle::CE_ItemViewItem, &base, painter, option.widget);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const QRect area = option.rect.adjusted(10, 6, -10, -6);
    if (kind == CellKind::Priority || kind == CellKind::Status) {
        const QString text = index.data().toString();
        const QString key = QString(text).remove(' ').toLower();
        QString tone = "gray";
        if (key == "high" || key == "urgent" || key == "overdue") tone = "red";
        else if (key == "medium") tone = "amber";
        else if (key == "low" || key == "completed" || key == "active") tone = "green";
        else if (key == "inprogress") tone = "blue";
        const QColor foreground = tone == "gray" ? (dark() ? QColor("#c4cedf") : QColor("#354261")) : toneColor(tone);
        const QColor background = mix(dark() ? QColor("#202b3c") : QColor(Qt::white), foreground, dark() ? .18 : .12);
        QFont font(option.font);
        font.setPixelSize(12);
        painter->setFont(font);
        const int width = std::min(area.width(), QFontMetrics(font).horizontalAdvance(text) + 18);
        QRect pill(area.x(), area.center().y() - 11, width, 22);
        painter->setPen(Qt::NoPen);
        painter->setBrush(background);
        painter->drawRoundedRect(pill, 7, 7);
        painter->setPen(foreground);
        painter->drawText(pill.adjusted(5, 0, -5, 0), Qt::AlignCenter, QFontMetrics(font).elidedText(text, Qt::ElideRight, width - 10));
    } else if (kind == CellKind::Progress) {
        const int value = std::clamp(index.data(NumberRole).toInt(), 0, 100);
        const int barWidth = std::clamp(area.width() - 42, 28, 92);
        QRectF bar(area.x(), area.center().y() - 4, barWidth, 8);
        painter->setPen(Qt::NoPen);
        painter->setBrush(dark() ? QColor("#2d3748") : QColor("#e8eff8"));
        painter->drawRoundedRect(bar, 4, 4);
        if (value > 0) {
            QLinearGradient fill(bar.topLeft(), bar.topRight());
            fill.setColorAt(0, QColor("#087bff")); fill.setColorAt(1, QColor("#62adff"));
            painter->setBrush(fill);
            auto current = bar; current.setWidth(bar.width() * value / 100.0);
            painter->drawRoundedRect(current, 4, 4);
        }
        painter->setPen(dark() ? QColor("#a5b2c9") : QColor("#697c9e"));
        painter->drawText(QRect(area.x() + barWidth + 8, area.y(), 36, area.height()), Qt::AlignVCenter, QString::number(value) + '%');
    } else if (kind == CellKind::Avatars) {
        const auto names = index.data(NamesRole).toStringList();
        for (int i = 0; i < std::min(3, static_cast<int>(names.size())); ++i)
            drawAvatar(*painter, QRectF(area.x() + i * 20, area.center().y() - 13, 26, 26), names[i]);
        if (names.isEmpty()) {
            painter->setPen(option.palette.color(QPalette::PlaceholderText));
            painter->drawText(area, Qt::AlignVCenter, "—");
        }
    }
    painter->restore();
}

AnimatedButton::AnimatedButton(const QString& text, QWidget* parent) : QPushButton(text, parent), hoverAnimation_(new QVariantAnimation(this)) {
    hoverAnimation_->setDuration(140);
    hoverAnimation_->setEasingCurve(QEasingCurve::OutCubic);
    connect(hoverAnimation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { hover_ = v.toReal(); update(); });
}
void AnimatedButton::setHover(qreal target) {
    if (!motionAllowed()) { hover_ = target; update(); return; }
    hoverAnimation_->stop();
    hoverAnimation_->setStartValue(hover_);
    hoverAnimation_->setEndValue(target);
    hoverAnimation_->start();
}
void AnimatedButton::enterEvent(QEnterEvent* event) { QPushButton::enterEvent(event); setHover(1); }
void AnimatedButton::leaveEvent(QEvent* event) { QPushButton::leaveEvent(event); setHover(0); }
void AnimatedButton::paintEvent(QPaintEvent* event) {
    QPushButton::paintEvent(event);
    if (hover_ <= .001 || !isEnabled() || property("link").toBool() || objectName() == "navButton") return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(80, 150, 255, static_cast<int>(hover_ * (isDown() ? 28 : 12))));
    painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 7, 7);
}

IconTile::IconTile(const QString& icon, const QString& tone, int size, QWidget* parent)
    : QWidget(parent), icon_(icon), tone_(tone) { setFixedSize(size, size); setAttribute(Qt::WA_TranslucentBackground); }
void IconTile::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor color = toneColor(tone_);
    QLinearGradient background(0, 0, width(), height());
    background.setColorAt(0, mix(dark() ? QColor("#202b3b") : QColor(Qt::white), color, dark() ? .32 : .13));
    background.setColorAt(1, mix(dark() ? QColor("#192231") : QColor(Qt::white), color, dark() ? .20 : .07));
    painter.setBrush(background);
    painter.setPen(Qt::NoPen);
    if (property("circular").toBool()) painter.drawEllipse(rect().adjusted(0, 0, -1, -1));
    else painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 9, 9);
    const int side = static_cast<int>(width() * .50);
    const QRect target((width() - side)/2, (height() - side)/2, side, side);
    // The reference's key metric symbols use simple filled forms. The rest of
    // the application continues to use the same Lucide outline icon family.
    if (icon_ == "projects" || icon_ == "folder" || icon_ == "tasks" || icon_ == "check" || icon_ == "teams") {
        painter.save();
        painter.translate(target.topLeft());
        painter.scale(side / 24.0, side / 24.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        if (icon_ == "projects" || icon_ == "folder") {
            QPainterPath folder;
            folder.moveTo(3, 5); folder.quadTo(3, 3, 5, 3);
            folder.lineTo(10, 3); folder.lineTo(13, 6); folder.lineTo(20, 6);
            folder.quadTo(22, 6, 22, 8); folder.lineTo(22, 19);
            folder.quadTo(22, 21, 20, 21); folder.lineTo(4, 21);
            folder.quadTo(2, 21, 2, 19); folder.lineTo(2, 7); folder.quadTo(2, 5, 3, 5);
            painter.drawPath(folder);
        } else if (icon_ == "teams") {
            painter.drawEllipse(QRectF(8.5, 3, 7, 7));
            painter.drawEllipse(QRectF(2.5, 6, 5, 5));
            painter.drawEllipse(QRectF(16.5, 6, 5, 5));
            painter.drawRoundedRect(QRectF(7, 12, 10, 10), 4, 4);
            painter.drawRoundedRect(QRectF(0.5, 13, 7, 8), 3, 3);
            painter.drawRoundedRect(QRectF(16.5, 13, 7, 8), 3, 3);
        } else if (tone_ == "green") {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(color, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawEllipse(QRectF(2, 2, 20, 20));
            painter.drawPolyline(QPolygonF({QPointF(7, 12), QPointF(10.5, 15.5), QPointF(17, 8.5)}));
        } else {
            painter.drawRoundedRect(QRectF(3, 3, 18, 18), 3, 3);
            painter.setPen(QPen(Qt::white, 2.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPolyline(QPolygonF({QPointF(7, 12), QPointF(10.5, 15.5), QPointF(17, 8.5)}));
        }
        painter.restore();
    } else {
        drawIcon(painter, icon_, target, color);
    }
}

ProfileButton::ProfileButton(const QString& name, const QString& role, QWidget* parent)
    : AnimatedButton({}, parent) {
    setObjectName("profileMenu");
    setProperty("profileMenu", true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setAutoDefault(false);
    setMinimumWidth(160);
    setFixedHeight(52);
    setProfile(name, role);
}
void ProfileButton::setProfile(const QString& name, const QString& role) {
    name_ = name;
    role_ = role;
    setAccessibleName(name + ", " + role);
    setToolTip(name + "\n" + role);
    update();
}
void ProfileButton::paintEvent(QPaintEvent* event) {
    AnimatedButton::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    drawAvatar(painter, QRectF(6, (height() - 32) / 2.0, 32, 32), name_);
    const int textWidth = std::max(10, width() - 75);
    QFont nameFont = QApplication::font();
    nameFont.setPixelSize(13);
    nameFont.setWeight(QFont::DemiBold);
    painter.setFont(nameFont);
    painter.setPen(palette().color(QPalette::ButtonText));
    painter.drawText(QRect(48, 8, textWidth, 18), Qt::AlignLeft | Qt::AlignVCenter,
                     QFontMetrics(nameFont).elidedText(name_, Qt::ElideRight, textWidth));
    QFont roleFont = QApplication::font();
    roleFont.setPixelSize(11);
    painter.setFont(roleFont);
    painter.setPen(dark() ? QColor("#96a7bd") : QColor("#71819e"));
    painter.drawText(QRect(48, 27, textWidth, 16), Qt::AlignLeft | Qt::AlignVCenter,
                     QFontMetrics(roleFont).elidedText(role_, Qt::ElideRight, textWidth));
    drawIcon(painter, "chevron-down", QRect(width() - 22, (height() - 14) / 2, 14, 14),
             dark() ? QColor("#96a7bd") : QColor("#71819e"));
}
AvatarStack::AvatarStack(QWidget* parent) : QWidget(parent) { setFixedSize(78, 30); }
void AvatarStack::setNames(const QStringList& names) { names_ = names; setToolTip(names.join(", ")); update(); }
void AvatarStack::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    for (int i = 0; i < std::min(3, static_cast<int>(names_.size())); ++i)
        drawAvatar(painter, QRectF(1 + i * 20, 2, 26, 26), names_[i]);
}
ReferenceBackdrop::ReferenceBackdrop(QWidget* parent) : QWidget(parent) { setObjectName("referenceLoginBackdrop"); }
void ReferenceBackdrop::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QLinearGradient background(0, 0, width(), height());
    background.setColorAt(0, dark() ? QColor("#101c31") : QColor("#f8fcff"));
    background.setColorAt(.48, dark() ? QColor("#142a48") : QColor("#dfedff"));
    background.setColorAt(1, dark() ? QColor("#101a29") : QColor("#cfe8ff"));
    painter.fillRect(rect(), background);
    painter.setPen(Qt::NoPen);
    painter.setBrush(dark() ? QColor(53, 102, 164, 18) : QColor(255, 255, 255, 75));
    const qreal unit = height() * .44;
    painter.save();
    painter.translate(width() * .33, height() * .04);
    painter.rotate(38);
    painter.drawRoundedRect(QRectF(0, 0, unit, unit), 48, 48);
    painter.restore();
    painter.setBrush(dark() ? QColor(63, 119, 189, 12) : QColor(113, 184, 255, 22));
    painter.drawEllipse(QRectF(-unit * .5, height() * .55, unit * 1.6, unit * 1.6));
    painter.drawEllipse(QRectF(width() * .65, -unit * .9, unit * 1.7, unit * 1.7));
}

} // namespace teamsync::ui
