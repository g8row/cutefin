#pragma once

#include "api/Models.h"
#include "app/MediaItemModel.h"

#include <QAbstractListModel>
#include <QList>
#include <QString>

#include <optional>

// Holds the dynamic vertical list of Home carousels (Continue Watching, Next Up,
// Latest Movies, Latest Shows, ...). Each row owns a MediaItemModel* so QML
// ListViews bind directly to virtualized C++ models with zero JS array copying.
class HomeSectionModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        KeyRole = Qt::UserRole + 1,
        TitleRole,
        KindRole,      // "resume", "nextup", "latest"
        LandscapeRole, // true for Continue Watching / Next Up 16:9 cards
        LibraryIdRole,
        ItemsModelRole,
    };
    Q_ENUM(Roles)

    explicit HomeSectionModel(QObject *parent = nullptr);
    ~HomeSectionModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void clear();
    void upsertSection(const QString &key, const QString &title, const QString &kind, bool landscape,
                       const QString &libraryId, int sortOrder, const QList<MediaItem> &items);
    void removeSection(const QString &key);
    void updateUserState(const QString &itemId, bool played, bool isFavorite);
    std::optional<MediaItem> findItemById(const QString &itemId) const;

signals:
    void countChanged();

private:
    struct Section {
        QString key;
        QString title;
        QString kind;
        bool landscape = false;
        QString libraryId;
        int sortOrder = 0;
        MediaItemModel *model = nullptr;
    };

    QList<Section> m_sections;
};
