#ifndef MOVIEWIDGET_H
#define MOVIEWIDGET_H

#include <QWidget>
#include <qlabel.h>
#include <qpushbutton.h>

#include "movie.h"
#include "jellyfinapi.h"
#include "mediawidget.h"

class MovieWidget : public MediaWidget
{
    Q_OBJECT
public:
    explicit MovieWidget(const Movie& _movie, JellyfinApi *_api, QWidget *parent = nullptr);
signals:
    void playRequested(const QString &movieId);
    void markPlayedRequested(const QString &movieId);
    void favoriteToggled(const QString &movieId, bool favorite);

private slots:
    void onPlayClicked();
    void onMarkPlayedClicked();
    void onFavoriteClicked();

private:
    Movie movie;
    JellyfinApi *jellyfinApi;
    QLabel *bannerLabel;
    QLabel *primaryLabel;
    QLabel *titleLabel;
    QLabel *descriptionLabel;
    QPushButton *backButton;
    QPushButton *playButton;
    QPushButton *markPlayedButton;
    QPushButton *favoriteButton;
    bool isFavorite;

    void fetchImages();
    void setBannerImage(const QPixmap &image);
    void setPrimaryImage(const QPixmap &image);
};

#endif // MOVIEWIDGET_H
