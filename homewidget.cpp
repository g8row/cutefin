#include "homewidget.h"
#include "moviecard.h"
#include "moviewidget.h"

#include <QScrollArea>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
HomeWidget::HomeWidget(JellyfinApi *api, QWidget *parent)
    : QWidget{parent}, jellyfinApi(api)
{
    QScrollArea *moviesArea = new QScrollArea();
    moviesArea->setWidgetResizable(true);
    QWidget *moviesWidget = new QWidget();
    QHBoxLayout *moviesLayout = new QHBoxLayout(moviesWidget);
    moviesWidget->setLayout(moviesLayout);
    moviesWidget->setMinimumHeight(300);
    moviesArea->setWidget(moviesWidget);
    moviesArea->setMinimumHeight(330);
    moviesArea->setMaximumHeight(330);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(new QLabel("Latest Movies", this));
    mainLayout->addWidget(moviesArea);

    connect(jellyfinApi, &JellyfinApi::latestMoviesResponse, this, [this, moviesWidget](bool success, QList<Movie> movies, const QString &errorMessage = QString()){
        if (success) {
            for (const Movie &movie : movies) {
                MovieCard *movieCard = new MovieCard(movie, moviesWidget);
                movieCard->fetchPoster(jellyfinApi);
                moviesWidget->layout()->addWidget(movieCard);
                connect(movieCard, &MovieCard::clicked, this, [this](const Movie& movie){
                    MovieWidget* movieWidget = new MovieWidget(movie, jellyfinApi, this->parentWidget());
                    emit mediaSelected(movieWidget);
                });
            }
        } else {
            qWarning() << "Failed to fetch movies:" << errorMessage;
        }
    });

    jellyfinApi->getLatestMovies();
}



HomeWidget::~HomeWidget(){

}
