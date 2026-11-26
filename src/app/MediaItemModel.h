#pragma once

#include "api/Models.h"

#include <QAbstractListModel>
#include <QList>
#include <QVariantMap>

#include <optional>

// Virtualized C++ list model backing QML poster grids, horizontal carousels,
// episode lists, search results, and library rail entries.
class MediaItemModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        TypeRole,
        CollectionTypeRole,
        OverviewRole,
        TaglineRole,
        YearRole,
        RuntimeTicksRole,
        RuntimeTextRole,
        CommunityRatingRole,
        RatingTextRole,
        OfficialRatingRole,
        GenresRole,
        PrimaryTagRole,
        ThumbTagRole,
        BackdropTagRole,
        BackdropItemIdRole,
        LogoTagRole,
        PrimaryBlurHashRole,
        ThumbBlurHashRole,
        BackdropBlurHashRole,
        SeriesIdRole,
        SeriesNameRole,
        SeasonIdRole,
        SeasonNameRole,
        IndexNumberRole,
        ParentIndexNumberRole,
        EpisodeCodeRole,
        SubtitleTextRole,
        PlayedRole,
        PlayedPercentageRole,
        PlaybackPositionTicksRole,
        ResumePositionTextRole,
        RemainingTextRole,
        IsFavoriteRole,
        IsPlayableRole,
    };
    Q_ENUM(Roles)

    explicit MediaItemModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setItems(const QList<MediaItem> &items);
    void appendItems(const QList<MediaItem> &items);
    void clear();

    const QList<MediaItem> &items() const { return m_items; }
    std::optional<MediaItem> itemById(const QString &id) const;
    Q_INVOKABLE QVariantMap get(int index) const;

    // Optimistic state updates mirrored across every live model.
    void updateUserState(const QString &itemId, bool played, bool isFavorite);

    static QVariantMap toVariantMap(const MediaItem &item);
    static QString episodeCodeFor(const MediaItem &item);
    static QString subtitleFor(const MediaItem &item);

signals:
    void countChanged();

private:
    QList<MediaItem> m_items;
};
