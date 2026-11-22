#pragma once

#include <QPushButton>
#include <QStyledItemDelegate>
#include <QWidget>
#include <QColor>
#include <QStringList>

class QVariantAnimation;
class QProgressBar;

namespace teamsync::ui {

enum ItemRole { KindRole = Qt::UserRole + 20, NumberRole, NamesRole };
enum class CellKind { Normal = 0, Priority, Status, Progress, Avatars };

bool motionAllowed();
QColor accentColor();
QIcon avatarIcon(const QString& name, int size = 32);
QString initials(const QString& name);
void crossFade(QWidget* page, const QPixmap& previous, int duration = 200);
void softShadow(QWidget* widget, int blur = 22, int yOffset = 5);
void installMotionEffects(QWidget* root);
void animateProgress(QProgressBar* progress, int value);

class ReferenceItemDelegate final : public QStyledItemDelegate {
public:
    explicit ReferenceItemDelegate(QObject* parent = nullptr);
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

class AnimatedButton : public QPushButton {
public:
    explicit AnimatedButton(const QString& text = {}, QWidget* parent = nullptr);
protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
private:
    qreal hover_ = 0;
    QVariantAnimation* hoverAnimation_;
    void setHover(qreal target);
};

class IconTile final : public QWidget {
public:
    IconTile(const QString& icon, const QString& tone = "blue", int size = 48, QWidget* parent = nullptr);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QString icon_, tone_;
};

class ProfileButton final : public AnimatedButton {
public:
    ProfileButton(const QString& name, const QString& role, QWidget* parent = nullptr);
    void setProfile(const QString& name, const QString& role);
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    QString name_, role_;
};

class AvatarStack final : public QWidget {
public:
    explicit AvatarStack(QWidget* parent = nullptr);
    void setNames(const QStringList& names);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QStringList names_;
};

class ReferenceBackdrop final : public QWidget {
public:
    explicit ReferenceBackdrop(QWidget* parent = nullptr);
protected:
    void paintEvent(QPaintEvent*) override;
};

} // namespace teamsync::ui
