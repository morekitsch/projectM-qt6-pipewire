#include "RatingDelegate.h"

#include <QComboBox>

RatingDelegate::RatingDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

QWidget *RatingDelegate::createEditor(QWidget *parent,
                                      const QStyleOptionViewItem &option,
                                      const QModelIndex &index) const {
  Q_UNUSED(option);
  Q_UNUSED(index);

  auto *combo = new QComboBox(parent);
  combo->addItem(QStringLiteral("1 star"), 1);
  combo->addItem(QStringLiteral("2 stars"), 2);
  combo->addItem(QStringLiteral("3 stars"), 3);
  combo->addItem(QStringLiteral("4 stars"), 4);
  combo->addItem(QStringLiteral("5 stars"), 5);
  connect(combo, qOverload<int>(&QComboBox::activated), this, [this, combo](int) {
    Q_EMIT const_cast<RatingDelegate *>(this)->commitData(combo);
    Q_EMIT const_cast<RatingDelegate *>(this)->closeEditor(combo);
  });
  return combo;
}

void RatingDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const {
  auto *combo = qobject_cast<QComboBox *>(editor);
  if (combo == nullptr) {
    return;
  }

  const int value = index.model()->data(index, Qt::EditRole).toInt();
  const int idx = combo->findData(value);
  combo->setCurrentIndex(idx >= 0 ? idx : 2);
}

void RatingDelegate::setModelData(QWidget *editor,
                                  QAbstractItemModel *model,
                                  const QModelIndex &index) const {
  auto *combo = qobject_cast<QComboBox *>(editor);
  if (combo == nullptr) {
    return;
  }

  model->setData(index, combo->currentData(), Qt::EditRole);
}

QString RatingDelegate::displayText(const QVariant &value, const QLocale &locale) const {
  Q_UNUSED(locale);

  const int rating = value.toInt();
  return QStringLiteral("%1/5").arg(rating);
}
