#include "app/HomeSectionModel.h"

HomeSectionModel::HomeSectionModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

// Section models are QObject children of this model and die with it.
HomeSectionModel::~HomeSectionModel() = default;

int HomeSectionModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_sections.size());
}

QVariant HomeSectionModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_sections.size())
        return QVariant();

    const Section &section = m_sections.at(index.row());
    switch (role) {
    case KeyRole:
        return section.key;
    case Qt::DisplayRole:
    case TitleRole:
        return section.title;
    case KindRole:
        return section.kind;
    case LandscapeRole:
        return section.landscape;
    case LibraryIdRole:
        return section.libraryId;
    case ItemsModelRole:
        return QVariant::fromValue(section.model);
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> HomeSectionModel::roleNames() const
{
    return {
        {KeyRole, QByteArrayLiteral("key")},
        {TitleRole, QByteArrayLiteral("title")},
        {KindRole, QByteArrayLiteral("kind")},
        {LandscapeRole, QByteArrayLiteral("landscape")},
        {LibraryIdRole, QByteArrayLiteral("libraryId")},
        {ItemsModelRole, QByteArrayLiteral("itemsModel")},
    };
}

void HomeSectionModel::clear()
{
    if (m_sections.isEmpty())
        return;

    beginResetModel();
    const QList<Section> removed = m_sections;
    m_sections.clear();
    endResetModel();

    // Deferred so delegates still being torn down never see a dead model.
    for (const Section &section : removed)
        section.model->deleteLater();
    emit countChanged();
}

void HomeSectionModel::upsertSection(const QString &key, const QString &title, const QString &kind, bool landscape,
                                     const QString &libraryId, int sortOrder, const QList<MediaItem> &items)
{
    if (items.isEmpty()) {
        removeSection(key);
        return;
    }

    int existing = -1;
    for (int row = 0; row < rowCount(); ++row) {
        if (m_sections.at(row).key == key) {
            existing = row;
            break;
        }
    }

    if (existing >= 0 && m_sections.at(existing).sortOrder == sortOrder) {
        Section &section = m_sections[existing];
        section.title = title;
        section.kind = kind;
        section.landscape = landscape;
        section.libraryId = libraryId;
        section.model->setItems(items);
        const QModelIndex changed = index(existing);
        emit dataChanged(changed, changed, {TitleRole, KindRole, LandscapeRole, LibraryIdRole});
        return;
    }

    MediaItemModel *model = nullptr;
    if (existing >= 0) {
        // The section moved: take it out and reinsert it, keeping its model.
        beginRemoveRows(QModelIndex(), existing, existing);
        model = m_sections.takeAt(existing).model;
        endRemoveRows();
    } else {
        model = new MediaItemModel(this);
    }
    model->setItems(items);

    int row = 0;
    while (row < rowCount() && m_sections.at(row).sortOrder <= sortOrder)
        ++row;

    beginInsertRows(QModelIndex(), row, row);
    m_sections.insert(row, Section{key, title, kind, landscape, libraryId, sortOrder, model});
    endInsertRows();

    if (existing < 0)
        emit countChanged();
}

void HomeSectionModel::removeSection(const QString &key)
{
    for (int row = 0; row < rowCount(); ++row) {
        if (m_sections.at(row).key != key)
            continue;
        beginRemoveRows(QModelIndex(), row, row);
        MediaItemModel *model = m_sections.takeAt(row).model;
        endRemoveRows();
        model->deleteLater();
        emit countChanged();
        return;
    }
}

void HomeSectionModel::updateUserState(const QString &itemId, bool played, bool isFavorite)
{
    for (const Section &section : std::as_const(m_sections))
        section.model->updateUserState(itemId, played, isFavorite);
}

std::optional<MediaItem> HomeSectionModel::findItemById(const QString &itemId) const
{
    for (const Section &section : m_sections) {
        if (std::optional<MediaItem> item = section.model->itemById(itemId))
            return item;
    }
    return std::nullopt;
}
