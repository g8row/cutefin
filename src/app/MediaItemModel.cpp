#include "app/MediaItemModel.h"

#include "core/Formatting.h"

namespace {

const QHash<int, QByteArray> &roleNameTable()
{
    static const QHash<int, QByteArray> names = {
        {MediaItemModel::IdRole, QByteArrayLiteral("id")},
        {MediaItemModel::NameRole, QByteArrayLiteral("name")},
        {MediaItemModel::TypeRole, QByteArrayLiteral("type")},
        {MediaItemModel::CollectionTypeRole, QByteArrayLiteral("collectionType")},
        {MediaItemModel::OverviewRole, QByteArrayLiteral("overview")},
        {MediaItemModel::TaglineRole, QByteArrayLiteral("tagline")},
        {MediaItemModel::YearRole, QByteArrayLiteral("year")},
        {MediaItemModel::RuntimeTicksRole, QByteArrayLiteral("runtimeTicks")},
        {MediaItemModel::RuntimeTextRole, QByteArrayLiteral("runtimeText")},
        {MediaItemModel::CommunityRatingRole, QByteArrayLiteral("communityRating")},
        {MediaItemModel::RatingTextRole, QByteArrayLiteral("ratingText")},
        {MediaItemModel::OfficialRatingRole, QByteArrayLiteral("officialRating")},
        {MediaItemModel::GenresRole, QByteArrayLiteral("genres")},
        {MediaItemModel::PrimaryTagRole, QByteArrayLiteral("primaryTag")},
        {MediaItemModel::ThumbTagRole, QByteArrayLiteral("thumbTag")},
        {MediaItemModel::BackdropTagRole, QByteArrayLiteral("backdropTag")},
        {MediaItemModel::BackdropItemIdRole, QByteArrayLiteral("backdropItemId")},
        {MediaItemModel::LogoTagRole, QByteArrayLiteral("logoTag")},
        {MediaItemModel::PrimaryBlurHashRole, QByteArrayLiteral("primaryBlurHash")},
        {MediaItemModel::ThumbBlurHashRole, QByteArrayLiteral("thumbBlurHash")},
        {MediaItemModel::BackdropBlurHashRole, QByteArrayLiteral("backdropBlurHash")},
        {MediaItemModel::SeriesIdRole, QByteArrayLiteral("seriesId")},
        {MediaItemModel::SeriesNameRole, QByteArrayLiteral("seriesName")},
        {MediaItemModel::SeasonIdRole, QByteArrayLiteral("seasonId")},
        {MediaItemModel::SeasonNameRole, QByteArrayLiteral("seasonName")},
        {MediaItemModel::IndexNumberRole, QByteArrayLiteral("indexNumber")},
        {MediaItemModel::ParentIndexNumberRole, QByteArrayLiteral("parentIndexNumber")},
        {MediaItemModel::EpisodeCodeRole, QByteArrayLiteral("episodeCode")},
        {MediaItemModel::SubtitleTextRole, QByteArrayLiteral("subtitleText")},
        {MediaItemModel::PlayedRole, QByteArrayLiteral("played")},
        {MediaItemModel::PlayedPercentageRole, QByteArrayLiteral("playedPercentage")},
        {MediaItemModel::PlaybackPositionTicksRole, QByteArrayLiteral("playbackPositionTicks")},
        {MediaItemModel::ResumePositionTextRole, QByteArrayLiteral("resumePositionText")},
        {MediaItemModel::RemainingTextRole, QByteArrayLiteral("remainingText")},
        {MediaItemModel::IsFavoriteRole, QByteArrayLiteral("isFavorite")},
        {MediaItemModel::IsPlayableRole, QByteArrayLiteral("isPlayable")},
    };
    return names;
}

QString backdropTagFor(const MediaItem &item)
{
    if (!item.backdropTags.isEmpty())
        return item.backdropTags.first();
    return item.parentBackdropTags.value(0);
}

QString backdropItemIdFor(const MediaItem &item)
{
    if (!item.backdropTags.isEmpty() || item.parentBackdropTags.isEmpty())
        return item.id;
    if (!item.parentBackdropItemId.isEmpty())
        return item.parentBackdropItemId;
    return item.seriesId.isEmpty() ? item.id : item.seriesId;
}

QString blurHashFor(const MediaItem &item, const QString &imageType, const QString &tag)
{
    if (tag.isEmpty())
        return QString();
    return item.blurHashes.value(imageType).value(tag);
}

double playedPercentageFor(const MediaItem &item)
{
    if (item.userData.playedPercentage > 0.0)
        return qMin(item.userData.playedPercentage, 100.0);
    if (item.userData.playbackPositionTicks > 0 && item.runTimeTicks > 0) {
        return qMin(100.0, 100.0 * static_cast<double>(item.userData.playbackPositionTicks)
                               / static_cast<double>(item.runTimeTicks));
    }
    return 0.0;
}

QString resumePositionTextFor(const MediaItem &item)
{
    if (item.userData.playbackPositionTicks <= 0)
        return QString();
    return Formatting::formatPosition(Formatting::ticksToSeconds(item.userData.playbackPositionTicks));
}

QString remainingTextFor(const MediaItem &item)
{
    const qint64 position = item.userData.playbackPositionTicks;
    if (position <= 0 || item.runTimeTicks <= position)
        return QString();
    return QStringLiteral("%1 left").arg(Formatting::formatRuntimeTicks(item.runTimeTicks - position));
}

bool isPlayableType(const QString &type)
{
    return type == QLatin1String("Movie") || type == QLatin1String("Episode") || type == QLatin1String("Video")
        || type == QLatin1String("MusicVideo");
}

QVariant roleValue(const MediaItem &item, int role)
{
    switch (role) {
    case MediaItemModel::IdRole:
        return item.id;
    case MediaItemModel::NameRole:
        return item.name;
    case MediaItemModel::TypeRole:
        return item.type;
    case MediaItemModel::CollectionTypeRole:
        return item.collectionType;
    case MediaItemModel::OverviewRole:
        return item.overview;
    case MediaItemModel::TaglineRole:
        return item.tagline;
    case MediaItemModel::YearRole:
        return item.productionYear;
    case MediaItemModel::RuntimeTicksRole:
        return item.runTimeTicks;
    case MediaItemModel::RuntimeTextRole:
        return item.runTimeTicks > 0 ? Formatting::formatRuntimeTicks(item.runTimeTicks) : QString();
    case MediaItemModel::CommunityRatingRole:
        return item.communityRating;
    case MediaItemModel::RatingTextRole:
        return Formatting::formatRating(item.communityRating);
    case MediaItemModel::OfficialRatingRole:
        return item.officialRating;
    case MediaItemModel::GenresRole:
        return item.genres;
    case MediaItemModel::PrimaryTagRole:
        return item.imageTags.value(QStringLiteral("Primary"));
    case MediaItemModel::ThumbTagRole:
        return item.imageTags.value(QStringLiteral("Thumb"));
    case MediaItemModel::BackdropTagRole:
        return backdropTagFor(item);
    case MediaItemModel::BackdropItemIdRole:
        return backdropItemIdFor(item);
    case MediaItemModel::LogoTagRole:
        return item.imageTags.value(QStringLiteral("Logo"));
    case MediaItemModel::PrimaryBlurHashRole:
        return blurHashFor(item, QStringLiteral("Primary"), item.imageTags.value(QStringLiteral("Primary")));
    case MediaItemModel::ThumbBlurHashRole:
        return blurHashFor(item, QStringLiteral("Thumb"), item.imageTags.value(QStringLiteral("Thumb")));
    case MediaItemModel::BackdropBlurHashRole:
        return blurHashFor(item, QStringLiteral("Backdrop"), backdropTagFor(item));
    case MediaItemModel::SeriesIdRole:
        return item.seriesId;
    case MediaItemModel::SeriesNameRole:
        return item.seriesName;
    case MediaItemModel::SeasonIdRole:
        return item.seasonId;
    case MediaItemModel::SeasonNameRole:
        return item.seasonName;
    case MediaItemModel::IndexNumberRole:
        return item.indexNumber;
    case MediaItemModel::ParentIndexNumberRole:
        return item.parentIndexNumber;
    case MediaItemModel::EpisodeCodeRole:
        return MediaItemModel::episodeCodeFor(item);
    case MediaItemModel::SubtitleTextRole:
        return MediaItemModel::subtitleFor(item);
    case MediaItemModel::PlayedRole:
        return item.userData.played;
    case MediaItemModel::PlayedPercentageRole:
        return playedPercentageFor(item);
    case MediaItemModel::PlaybackPositionTicksRole:
        return item.userData.playbackPositionTicks;
    case MediaItemModel::ResumePositionTextRole:
        return resumePositionTextFor(item);
    case MediaItemModel::RemainingTextRole:
        return remainingTextFor(item);
    case MediaItemModel::IsFavoriteRole:
        return item.userData.isFavorite;
    case MediaItemModel::IsPlayableRole:
        return isPlayableType(item.type);
    default:
        return QVariant();
    }
}

} // namespace

MediaItemModel::MediaItemModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int MediaItemModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_items.size());
}

QVariant MediaItemModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return QVariant();
    const MediaItem &item = m_items.at(index.row());
    return roleValue(item, role == Qt::DisplayRole ? static_cast<int>(NameRole) : role);
}

QHash<int, QByteArray> MediaItemModel::roleNames() const
{
    return roleNameTable();
}

void MediaItemModel::setItems(const QList<MediaItem> &items)
{
    // A refresh that returns the same items in the same order is applied in
    // place, so views keep their delegates and scroll position.
    bool sameIds = items.size() == m_items.size();
    for (qsizetype i = 0; sameIds && i < items.size(); ++i)
        sameIds = items.at(i).id == m_items.at(i).id;

    if (sameIds) {
        if (m_items.isEmpty())
            return;
        m_items = items;
        emit dataChanged(index(0), index(rowCount() - 1));
        return;
    }

    const qsizetype previousCount = m_items.size();
    beginResetModel();
    m_items = items;
    endResetModel();
    if (previousCount != m_items.size())
        emit countChanged();
}

void MediaItemModel::appendItems(const QList<MediaItem> &items)
{
    if (items.isEmpty())
        return;
    const int first = rowCount();
    beginInsertRows(QModelIndex(), first, first + static_cast<int>(items.size()) - 1);
    m_items.append(items);
    endInsertRows();
    emit countChanged();
}

void MediaItemModel::clear()
{
    if (m_items.isEmpty())
        return;
    beginResetModel();
    m_items.clear();
    endResetModel();
    emit countChanged();
}

std::optional<MediaItem> MediaItemModel::itemById(const QString &id) const
{
    for (const MediaItem &item : m_items) {
        if (item.id == id)
            return item;
    }
    return std::nullopt;
}

QVariantMap MediaItemModel::get(int index) const
{
    if (index < 0 || index >= m_items.size())
        return QVariantMap();
    return toVariantMap(m_items.at(index));
}

void MediaItemModel::updateUserState(const QString &itemId, bool played, bool isFavorite)
{
    for (int row = 0; row < rowCount(); ++row) {
        const MediaItem &current = m_items.at(row);
        if (current.id != itemId
            || (current.userData.played == played && current.userData.isFavorite == isFavorite)) {
            continue;
        }

        UserData &data = m_items[row].userData;
        if (data.played != played) {
            // The server drops the resume point whenever the played flag is
            // set or cleared; mirror that immediately.
            data.playbackPositionTicks = 0;
            data.playedPercentage = 0.0;
        }
        data.played = played;
        data.isFavorite = isFavorite;

        const QModelIndex changed = index(row);
        emit dataChanged(changed, changed,
                         {PlayedRole, PlayedPercentageRole, PlaybackPositionTicksRole, ResumePositionTextRole,
                          RemainingTextRole, IsFavoriteRole});
    }
}

QVariantMap MediaItemModel::toVariantMap(const MediaItem &item)
{
    QVariantMap map;
    const QHash<int, QByteArray> &names = roleNameTable();
    for (auto it = names.constBegin(); it != names.constEnd(); ++it)
        map.insert(QString::fromLatin1(it.value()), roleValue(item, it.key()));
    return map;
}

QString MediaItemModel::episodeCodeFor(const MediaItem &item)
{
    if (item.type != QLatin1String("Episode") || item.indexNumber < 0)
        return QString();

    const QString episode = QStringLiteral("E%1").arg(item.indexNumber, 2, 10, QLatin1Char('0'));
    if (item.parentIndexNumber < 0)
        return episode;
    return QStringLiteral("S%1").arg(item.parentIndexNumber, 2, 10, QLatin1Char('0')) + episode;
}

QString MediaItemModel::subtitleFor(const MediaItem &item)
{
    if (item.type == QLatin1String("Episode")) {
        QStringList parts;
        if (!item.seriesName.isEmpty())
            parts << item.seriesName;
        const QString code = episodeCodeFor(item);
        if (!code.isEmpty())
            parts << code;
        return parts.join(QStringLiteral(" · "));
    }
    if (item.type == QLatin1String("Season"))
        return item.seriesName;
    return item.productionYear > 0 ? QString::number(item.productionYear) : QString();
}
