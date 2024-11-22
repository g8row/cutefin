#ifndef MOVIECARD_H
#define MOVIECARD_H

#include "movie.h"
#include "jellyfinapi.h"
#include <QWidget>
#include <QLabel>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QPalette>

class MovieCard : public QWidget
{
    Q_OBJECT
public:
    explicit MovieCard(const Movie &movie, QWidget *parent = nullptr);
    QLabel *titleLabel;
    QLabel *posterLabel;
    Movie movie;
    void fetchPoster(JellyfinApi *jellyfinApi);
signals:
    void clicked(const Movie movie);
private:
    void mousePressEvent(QMouseEvent *event);
    void enterEvent(QEnterEvent *event);
    void leaveEvent(QEvent *event);
};

#endif // MOVIECARD_H
